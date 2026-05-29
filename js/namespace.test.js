#!/usr/bin/env bun
import { describe, test, expect, beforeAll, afterAll } from "bun:test";
import { Socket } from "node:net";
import { $ } from "zx";
import { readFileSync, writeFileSync, existsSync, unlinkSync, rmSync } from "fs";
import { cpus } from "os";

class RedisClient {
  constructor(url) {
    const match = url.match(/:(\d+)/);
    this.port = match ? parseInt(match[1]) : 6379;
    this.socket = null;
    this.connected = false;
    this.buffer = Buffer.alloc(0);
    this.queue = [];
    this.connectPromise = null;
  }

  async connect() {
    if (this.connected) return;
    if (this.connectPromise) return this.connectPromise;

    this.connectPromise = new Promise((resolve, reject) => {
      this.socket = new Socket();
      this.socket.connect(this.port, "127.0.0.1", () => {
        this.connected = true;
        resolve();
      });

      this.socket.on("data", (data) => {
        this.buffer = Buffer.concat([this.buffer, data]);
        this.processQueue();
      });

      this.socket.on("error", (err) => {
        reject(err);
      });

      this.socket.on("close", () => {
        this.connected = false;
        for (const { reject: rej } of this.queue) {
          rej(new Error("Connection closed"));
        }
        this.queue = [];
      });
    });

    return this.connectPromise;
  }

  async send(cmd, args = []) {
    await this.connect();
    return new Promise((resolve, reject) => {
      this.queue.push({ resolve, reject });
      const encoded = this.encode(cmd, args);
      this.socket.write(encoded);
    });
  }

  encode(cmd, args) {
    const parts = [cmd, ...args];
    let resp = `*${parts.length}\r\n`;
    for (const part of parts) {
      const s = String(part);
      resp += `$${Buffer.byteLength(s)}\r\n${s}\r\n`;
    }
    return resp;
  }

  processQueue() {
    while (this.queue.length > 0) {
      const parsed = this.parseResponse();
      if (!parsed) {
        break;
      }
      const { value, error } = parsed;
      const { resolve, reject } = this.queue.shift();
      if (error) {
        reject(error);
      } else {
        resolve(value);
      }
    }
  }

  parseResponse() {
    if (this.buffer.length === 0) return null;
    const idx = this.buffer.indexOf("\r\n");
    if (idx === -1) return null;

    const type = this.buffer[0];
    const line = this.buffer.toString("utf8", 1, idx);

    if (type === 43) { // '+'
      this.buffer = this.buffer.subarray(idx + 2);
      return { value: line };
    }

    if (type === 45) { // '-'
      this.buffer = this.buffer.subarray(idx + 2);
      return { error: new Error(line) };
    }

    if (type === 58) { // ':'
      this.buffer = this.buffer.subarray(idx + 2);
      return { value: parseInt(line) };
    }

    if (type === 36) { // '$'
      const len = parseInt(line);
      if (len === -1) {
        this.buffer = this.buffer.subarray(idx + 2);
        return { value: null };
      }
      if (this.buffer.length < idx + 2 + len + 2) return null;
      const val = this.buffer.toString("utf8", idx + 2, idx + 2 + len);
      this.buffer = this.buffer.subarray(idx + 2 + len + 2);
      return { value: val };
    }

    if (type === 42) { // '*'
      const count = parseInt(line);
      if (count === -1) {
        this.buffer = this.buffer.subarray(idx + 2);
        return { value: null };
      }
      const savedBuffer = this.buffer;
      this.buffer = this.buffer.subarray(idx + 2);
      const arr = [];
      for (let i = 0; i < count; i++) {
        const item = this.parseResponse();
        if (!item) {
          this.buffer = savedBuffer;
          return null;
        }
        if (item.error) {
          this.buffer = savedBuffer;
          return { error: item.error };
        }
        arr.push(item.value);
      }
      return { value: arr };
    }

    this.buffer = this.buffer.subarray(idx + 2);
    return { error: new Error("Unknown RESP type: " + String.fromCharCode(type)) };
  }

  close() {
    if (this.socket) {
      this.socket.destroy();
    }
  }
}

$.verbose = false;

const TEST_PORT = 16379,
  REQUIRE_PASS = "testpass",
  CONFIG_FILE = "eloqkv_test.ini",
  DATA_DIR = "eloq_test_data";

let server_process = null;

const autoClose = (client) => {
    client[Symbol.dispose] = () => client.close();
    return client;
  },
  authClient = async (password = REQUIRE_PASS) => {
    const client = new RedisClient("redis://localhost:" + TEST_PORT);
    try {
      await client.send("AUTH", [password]);
      client[Symbol.dispose] = () => {
        client.close();
      };
      return client;
    } catch (err) {
      client.close();
      throw err;
    }
  },
  expectToFail = async (promise, expected_error_substring) => {
    try {
      await promise;
      expect().unreachable();
    } catch (err) {
      expect(err.message).toContain(expected_error_substring);
    }
  };

beforeAll(async () => {
  if (!existsSync("./build/eloqkv")) {
    throw new Error("EloqKV 服务端未编译，请先编译项目。");
  }

  console.log("准备测试配置...");
  // 读取基准配置并修改以实现测试隔离
  const base_config = readFileSync("eloqkv.ini", "utf-8");
  let test_config = base_config
    .replace(/port\s*=\s*\d+/, "port = " + TEST_PORT)
    .replace(/#\s*requirepass\s*=/, "requirepass = " + REQUIRE_PASS)
    .replace(/requirepass\s*=\s*$/, "requirepass = " + REQUIRE_PASS)
    .replace(/#\s*namespace\s*=\s*\w+/, "namespace = true")
    .replace(/namespace\s*=\s*\w+/, "namespace = true")
    .replace(/eloq_data_path\s*=\s*\S+/, "eloq_data_path = " + DATA_DIR);

  if (!test_config.includes("port = " + TEST_PORT)) {
    test_config += "\nport = " + TEST_PORT + "\n";
  }
  if (!test_config.includes("requirepass = " + REQUIRE_PASS)) {
    test_config += "\nrequirepass = " + REQUIRE_PASS + "\n";
  }
  if (!test_config.includes("namespace = true")) {
    test_config += "\nnamespace = true\n";
  }

  writeFileSync(CONFIG_FILE, test_config);

  console.log("启动 EloqKV 服务端...");
  server_process = $`./build/eloqkv --config=${CONFIG_FILE} > eloqkv_server.log 2>&1`;

  console.log("等待服务就绪（双重检查逻辑）...");
  let ready = false;
  for (let retry = 0; retry < 50; ++retry) {
    try {
      const socket = await Bun.connect({
        hostname: "127.0.0.1",
        port: TEST_PORT,
        socket: {
          data() {},
          open(s) {
            s.end();
          },
          error() {},
        },
      });
      socket.end();

      const test_client = new RedisClient("redis://localhost:" + TEST_PORT);
      try {
        await test_client.send("AUTH", [REQUIRE_PASS]);
        const pong = await test_client.send("PING", []);
        if (pong === "PONG") {
          ready = true;
          break;
        }
      } catch (err) {
        // 来自服务端的已认证或 NOAUTH 响应意味着端口已激活
        if (
          err.message &&
          (err.message.includes("NOAUTH") || err.message.includes("Authentication required"))
        ) {
          ready = true;
          break;
        }
      } finally {
        test_client.close();
      }
    } catch {
      // 忽略：套接字错误时重试
    }
    await new Promise((resolve) => setTimeout(resolve, 200));
  }

  if (!ready) {
    throw new Error("启动 EloqKV 服务端失败：重试后服务仍未就绪。");
  }
  console.log("EloqKV 服务端已就绪。");
}, 120000);

afterAll(async () => {
  console.log("清理资源...");
  if (server_process) {
    try {
      await server_process.kill("SIGKILL");
    } catch {
      // 忽略
    }
  }

  if (existsSync(CONFIG_FILE)) {
    try {
      unlinkSync(CONFIG_FILE);
    } catch {}
  }

  if (existsSync(DATA_DIR)) {
    try {
      rmSync(DATA_DIR, { recursive: true, force: true });
    } catch {}
  }
  console.log("清理完毕。");
});

describe("EloqKV 命名空间隔离与管理", () => {
  test("默认命名空间 current 及身份验证", async () => {
    using client = autoClose(new RedisClient("redis://localhost:" + TEST_PORT));

    await expectToFail(client.send("PING", []), "NOAUTH");

    const auth_res = await client.send("AUTH", [REQUIRE_PASS]);
    expect(auth_res).toBe("OK");

    const current_ns = await client.send("namespace", ["current"]);
    expect(current_ns).toBe("default");
  });

  test("通过默认客户端添加、获取、列出和删除命名空间", async () => {
    using client = await authClient();

    const token = await client.send("namespace", ["add", "ns_test_1"]);
    expect(typeof token).toBe("string");
    expect(token.length).toBeGreaterThan(0);

    await expectToFail(client.send("namespace", ["add", "ns_test_1"]), "already exists");
    await expectToFail(client.send("namespace", ["add", "default"]), "forbidden");

    const fetched_token = await client.send("namespace", ["get", "ns_test_1"]);
    expect(fetched_token).toBe(token);

    await expectToFail(client.send("namespace", ["get", "non_existent"]), "not found");

    const list = await client.send("namespace", ["get", "*"]);
    expect(Array.isArray(list)).toBe(true);
    // 列表返回命名空间/token 键值对（扁平数组）
    const ns_map = {};
    for (let i = 0; i < list.length; i += 2) {
      ns_map[list[i]] = list[i + 1];
    }
    expect(ns_map["ns_test_1"]).toBe(token);

    const new_token = await client.send("namespace", ["refresh", "ns_test_1"]);
    expect(typeof new_token).toBe("string");
    expect(new_token).not.toBe(token);

    const fetched_new_token = await client.send("namespace", ["get", "ns_test_1"]);
    expect(fetched_new_token).toBe(new_token);

    await expectToFail(client.send("namespace", ["refresh", "default"]), "forbidden");

    const rm_res = await client.send("namespace", ["del", "ns_test_1"]);
    expect(rm_res).toBe("OK");

    await expectToFail(client.send("namespace", ["del", "default"]), "forbidden");
    await expectToFail(client.send("namespace", ["del", "ns_test_1"]), "not found");
  });

  test("命名空间连接与数据隔离", async () => {
    using default_client = await authClient();
    const ns_name = "ns_isolation_test";

    const token = await default_client.send("namespace", ["add", [ns_name]]);

    // 自动在作用域结束时删除命名空间
    await using _ns_cleanup = {
      [Symbol.asyncDispose]: async () => {
        try {
          await default_client.send("namespace", ["del", [ns_name]]);
        } catch {
          // 清理时忽略删除失败
        }
      },
    };

    using ns_client = await authClient(token);

    const current_ns = await ns_client.send("namespace", ["current"]);
    expect(current_ns).toBe(ns_name);

    await ns_client.send("SET", ["shared_key", "value_custom"]);
    const custom_val = await ns_client.send("GET", ["shared_key"]);
    expect(custom_val).toBe("value_custom");

    const default_val_before = await default_client.send("GET", ["shared_key"]);
    expect(default_val_before).toBeNull();

    await default_client.send("SET", ["shared_key", "value_default"]);
    const default_val_after = await default_client.send("GET", ["shared_key"]);
    expect(default_val_after).toBe("value_default");

    const custom_val_after = await ns_client.send("GET", ["shared_key"]);
    expect(custom_val_after).toBe("value_custom");
  });

  test("自定义命名空间客户端的权限限制", async () => {
    using default_client = await authClient();
    const ns_name = "ns_perm_test";

    const token = await default_client.send("namespace", ["add", [ns_name]]);

    // 自动在作用域结束时删除命名空间
    await using _ns_cleanup = {
      [Symbol.asyncDispose]: async () => {
        try {
          await default_client.send("namespace", ["del", [ns_name]]);
        } catch {
          // 清理时忽略删除失败
        }
      },
    };

    using ns_client = await authClient(token);

    // 仅允许 requirepass 用户（默认命名空间）管理命名空间
    await expectToFail(
      ns_client.send("namespace", ["add", "ns_sub"]),
      "only requirepass user is allowed to manage namespaces",
    );

    await expectToFail(
      ns_client.send("namespace", ["get", "*"]),
      "only requirepass user is allowed to manage namespaces",
    );
  });

  test("命名空间删除级联清空数据", async () => {
    using default_client = await authClient();
    const ns_name = "ns_cascade_del_test";

    const token = await default_client.send("namespace", ["add", [ns_name]]);
    using ns_client = await authClient(token);

    // 写入3个键
    await ns_client.send("SET", ["k1", "v1"]);
    await ns_client.send("SET", ["k2", "v2"]);
    await ns_client.send("SET", ["k3", "v3"]);

    // 获取当前DBSIZE
    const db_size_before = await default_client.send("DBSIZE", []);

    // 删除命名空间
    const rm_res = await default_client.send("namespace", ["del", [ns_name]]);
    expect(rm_res).toBe("OK");

    // 校验级联删除后，DBSIZE 减少了3个键
    const db_size_after = await default_client.send("DBSIZE", []);
    expect(db_size_before - db_size_after).toBe(3);
  });

  test("命名空间下 FLUSHDB 数据隔离与清空", async () => {
    using default_client = await authClient();
    const ns_name = "ns_flushdb_test";

    const token = await default_client.send("namespace", ["add", [ns_name]]);
    using ns_client = await authClient(token);

    // 写入测试数据到命名空间
    await ns_client.send("SET", ["ns_k1", "ns_v1"]);
    await ns_client.send("SET", ["ns_k2", "ns_v2"]);

    // 写入测试数据到默认命名空间
    await default_client.send("SET", ["def_k1", "def_v1"]);

    // 校验命名空间的大小为 2
    const ns_dbsize_before = await ns_client.send("DBSIZE", []);
    expect(ns_dbsize_before).toBe(2);

    // 在命名空间下执行 FLUSHDB
    const flush_res = await ns_client.send("FLUSHDB", []);
    expect(flush_res).toBe("OK");

    // 校验命名空间下的键已被清空
    const ns_dbsize_after = await ns_client.send("DBSIZE", []);
    expect(ns_dbsize_after).toBe(0);
    expect(await ns_client.send("GET", ["ns_k1"])).toBeNull();

    // 校验默认空间下的键不受影响
    expect(await default_client.send("GET", ["def_k1"])).toBe("def_v1");

    // 清理默认空间的测试键
    await default_client.send("DEL", ["def_k1"]);
    await default_client.send("namespace", ["del", [ns_name]]);
  });

  test("命名空间下 FLUSHALL 数据隔离与清空", async () => {
    using default_client = await authClient();
    const ns_name = "ns_flushall_test";

    const token = await default_client.send("namespace", ["add", [ns_name]]);
    using ns_client = await authClient(token);

    // 写入测试数据到命名空间
    await ns_client.send("SET", ["ns_k1", "ns_v1"]);

    // 写入测试数据到默认命名空间
    await default_client.send("SET", ["def_k1", "def_v1"]);

    // 校验命名空间的大小为 1
    const ns_dbsize_before = await ns_client.send("DBSIZE", []);
    expect(ns_dbsize_before).toBe(1);

    // 在命名空间下执行 FLUSHALL
    const flush_res = await ns_client.send("FLUSHALL", []);
    expect(flush_res).toBe("OK");

    // 校验命名空间下的键已被清空
    const ns_dbsize_after = await ns_client.send("DBSIZE", []);
    expect(ns_dbsize_after).toBe(0);
    expect(await ns_client.send("GET", ["ns_k1"])).toBeNull();

    // 校验默认空间下的键不受影响
    expect(await default_client.send("GET", ["def_k1"])).toBe("def_v1");

    // 清理默认空间的测试键
    await default_client.send("DEL", ["def_k1"]);
    await default_client.send("namespace", ["del", [ns_name]]);
  });

  test("自定义命名空间下禁止执行 SELECT", async () => {
    using default_client = await authClient();
    const ns_name = "ns_select_test";

    const token = await default_client.send("namespace", ["add", [ns_name]]);
    using ns_client = await authClient(token);

    // 默认空间支持 SELECT
    const def_select_res = await default_client.send("SELECT", [1]);
    expect(def_select_res).toBe("OK");
    // 切回 DB 0
    await default_client.send("SELECT", [0]);

    // 自定义命名空间禁止 SELECT
    await expectToFail(
      ns_client.send("SELECT", [1]),
      "SELECT is not allowed in custom namespace",
    );

    await default_client.send("namespace", ["del", [ns_name]]);
  });

  test("MULTI/EXEC 事务内 NAMESPACE ADD/REFRESH 正常运行且不双重执行", async () => {
    using client = await authClient();

    await client.send("MULTI", []);
    await client.send("namespace", ["add", "multi_ns_test"]);
    const exec_res = await client.send("EXEC", []);

    // 验证是否只执行了一次并且成功返回了 Token
    expect(Array.isArray(exec_res)).toBe(true);
    expect(exec_res.length).toBe(1);
    expect(typeof exec_res[0]).toBe("string");

    const token = exec_res[0];

    // 测试 REFRESH 在事务内也正常
    await client.send("MULTI", []);
    await client.send("namespace", ["refresh", "multi_ns_test"]);
    const refresh_exec_res = await client.send("EXEC", []);

    expect(Array.isArray(refresh_exec_res)).toBe(true);
    expect(refresh_exec_res.length).toBe(1);
    expect(typeof refresh_exec_res[0]).toBe("string");
    expect(refresh_exec_res[0]).not.toBe(token);

    // 清理
    const del_res = await client.send("namespace", ["del", "multi_ns_test"]);
    expect(del_res).toBe("OK");
  });

  test("自定义命名空间下 SCAN 和 KEYS 的返回键正常剥离命名空间前缀", async () => {
    using default_client = await authClient();
    const ns_name = "ns_scan_test";

    const token = await default_client.send("namespace", ["add", [ns_name]]);
    await using _ns_cleanup = {
      [Symbol.asyncDispose]: async () => {
        try {
          await default_client.send("namespace", ["del", [ns_name]]);
        } catch {}
      },
    };

    using ns_client = await authClient(token);

    // 写入一些测试键
    await ns_client.send("SET", ["user:1", "alice"]);
    await ns_client.send("SET", ["user:2", "bob"]);
    await ns_client.send("SET", ["product:1", "item"]);

    // 测试 KEYS
    const keys_all = await ns_client.send("KEYS", ["*"]);
    expect(Array.isArray(keys_all)).toBe(true);
    expect(keys_all.sort()).toEqual(["product:1", "user:1", "user:2"].sort());

    // 测试带通配符匹配的 KEYS
    const keys_matched = await ns_client.send("KEYS", ["user:*"]);
    expect(Array.isArray(keys_matched)).toBe(true);
    expect(keys_matched.sort()).toEqual(["user:1", "user:2"].sort());

    // 测试 SCAN
    const scan_res = await ns_client.send("SCAN", ["0"]);
    expect(Array.isArray(scan_res)).toBe(true);
    expect(scan_res.length).toBe(2);
    const scanned_keys = scan_res[1];
    expect(scanned_keys.sort()).toEqual(["product:1", "user:1", "user:2"].sort());

    // 测试 SCAN MATCH
    const scan_match_res = await ns_client.send("SCAN", ["0", "MATCH", "user:*"]);
    expect(Array.isArray(scan_match_res)).toBe(true);
    const scanned_matched_keys = scan_match_res[1];
    expect(scanned_matched_keys.sort()).toEqual(["user:1", "user:2"].sort());
  });

  test("并发 AUTH 不同的命名空间以验证连接与事务隔离安全性", async () => {
    using default_client = await authClient();
    
    // 批量添加多个命名空间
    const tokens = await Promise.all([
      default_client.send("namespace", ["add", "ns_concurrent_1"]),
      default_client.send("namespace", ["add", "ns_concurrent_2"]),
      default_client.send("namespace", ["add", "ns_concurrent_3"]),
    ]);

    await using _ns_cleanup = {
      [Symbol.asyncDispose]: async () => {
        try {
          await default_client.send("namespace", ["del", "ns_concurrent_1"]);
          await default_client.send("namespace", ["del", "ns_concurrent_2"]);
          await default_client.send("namespace", ["del", "ns_concurrent_3"]);
        } catch {}
      },
    };

    // 并发认证
    const clients = await Promise.all(tokens.map(t => authClient(t)));
    
    await using _clients_cleanup = {
      [Symbol.asyncDispose]: async () => {
        clients.forEach(c => c.close());
      }
    };

    // 并发验证
    const current_nss = await Promise.all(clients.map(c => c.send("namespace", ["current"])));
    expect(current_nss).toEqual(["ns_concurrent_1", "ns_concurrent_2", "ns_concurrent_3"]);
  });
});

