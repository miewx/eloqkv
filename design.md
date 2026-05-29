# EloqKV 命名空间设计与核心架构

本文件详述了修改后的 EloqKV C++ 命名空间隔离与管理机制的核心设计架构。所有设计均围绕代码的实际差异和重构后的全新命名空间逻辑展开。

---

## 0. 隔离与多租户隔离架构设计 (Isolation & Multi-Tenant Design)

命名空间隔离机制是 EloqKV 服务端默认开启的内置功能，不需要任何显式的开关配置：
- **默认命名空间（`default`）**：默认数据是**无前缀的（prefixless）**，数据直接路由至原有的各个物理数据库表（如 `data_table_0`, `data_table_1` 等），其行为与原生 Redis 逻辑完全一致。
- **自定义命名空间**：所有自定义命名空间均共享单个物理表 `ns_data_table`。通过 `AUTH <token>` 认证的客户端，其数据会自动路由到 `ns_data_table` 内，采用独特的 Base-255 编码前缀实现完全隔离。这避免了为每个命名空间创建物理表的系统开销。

---

## 1. 核心模型与隔离机制 (Core Model & Isolation Mechanism)

命名空间机制允许在同一个物理 Key-Value 实例上，通过虚拟前缀实现完全隔离的多租户数据环境。

### 1.1 bthread 协程局部隔离 (bthread-local Isolation)
由于 EloqKV 采用 `bthread`（M:N 协程调度框架）处理高并发请求，跨协程的数据隔离设计如下：
- **`GetCurrentNamespace()`**：统一获取当前上下文绑定的命名空间。
  - 使用 `bthread_key_t` 在协程级别独立分配 `std::string`。
  - 通过 `bthread_self() != 0` 区分当前在 `bthread` 还是传统 `pthread` 上下文。
  - 若在 `bthread` 协程中运行，使用 `bthread_getspecific` / `bthread_setspecific` 实现协程级本地存储（Coroutine-Local Storage），彻底消除物理线程复用带来的数据越权污染。
  - 若在外部线程中运行，安全回退到传统的 `thread_local`。
- **`NamespaceGuard`**：使用 RAII 模式，在处理客户端请求的生命周期内，动态切换并自动恢复 `current_namespace` 变量。

### 1.2 键前缀隔离与范围扫描 (Key Prefixing & Range Scan Isolation)
- **物理表级隔离与前缀隔离并存**：
  - **默认命名空间（`default`）**：默认情况下，默认命名空间的数据是**无前缀的（prefixless）**，保持原生的 Key 编码。其数据直接路由至原有的物理数据库表（如 `data_table_0`, `data_table_1` 等）。
  - **自定义命名空间**：所有自定义命名空间共享单个物理表 `ns_data_table`。
    - 各自定义命名空间在 `ns_data_table` 内使用独特的 Base-255 编码前缀实现前缀隔离。
    - **自定义命名空间 Key 前缀格式**：
      ```
      Key Prefix = encoded_ns_id + Delimiter (\x00) + encoded_epoch + Delimiter (\x00)
      ```

      ```mermaid
      graph TD
          NS_ID["encoded_ns_id (Base-255 string)"] --> DELIM1["Delimiter (1B: \\x00)"]
          DELIM1 --> EPOCH["encoded_epoch (Base-255 string)"]
          EPOCH --> DELIM2["Delimiter (1B: \\x00)"]
          DELIM2 --> USER_KEY["User Key (raw string)"]
      ```

      - **`encoded_ns_id`**：租户 Namespace ID 经过 Base-255 编码后的字符串。由于 Base-255 编码排除了 `\x00` 字符，因此 `\x00` 可以安全作为分隔符。
      - **`encoded_epoch`**：当前命名空间的 epoch（清除版本号），同样使用 Base-255 编码。
    - 由于 `\x00` 作为前缀分隔符且与数据内容完全隔离，保证了各个命名空间 Key 之间的无碰撞与安全隔离。
- **透明包装**：
  - `EloqKey` 在构造时通过 `CreateEloqStringFromNamespace` 透明地加上当前的命名空间前缀（对于默认命名空间不加前缀）。
- **范围限制与上限边界计算 (`ComposeNamespaceKeyNext`)**：
  - 针对 `KEYS` / `SCAN` 范围查找，通过将起始 Key 定位为当前租户的前缀（`prefix`），结束 Key 定位为 `ComposeNamespaceKeyNext(prefix)`（即调用 `NamespacePrefix::MakePrefixNext(prefix)`），将检索边界严格限定在当前命名空间和当前 Epoch 范围内。
  - **上限边界计算原理**：`MakePrefixNext` 自后向前查找第一个非 `\xFF` 的字符将其加 1，并截断其后的部分，在 B-Tree 范围查询时用作排他的上限边界。

### 1.3 核心实现代码说明

```cpp
// 1. Base-255 编码与解码实现 (include/b255_encode.h / src/b255_encode.cpp)
// 将数字 ID 转换为不含 \x00 字符的 Base-255 字符串
std::string EncodeBase255(uint64_t id)
{
    if (id == 0)
    {
        return std::string(1, '\x01');
    }
    std::string result;
    uint64_t temp = id;
    while (temp > 0)
    {
        uint64_t digit = temp % 255;
        char c = static_cast<char>(digit + 1); // 加上偏移避开 \x00
        result.push_back(c);
        temp /= 255;
    }
    std::reverse(result.begin(), result.end());
    return result;
}

// 2. 命名空间 Key 前缀构造 (include/namespace/prefix.h)
namespace NamespacePrefix
{
    constexpr char B255_DELIMITER = '\x00';

    inline std::string MakePrefix(std::string_view encoded_ns_id, uint64_t epoch)
    {
        std::string prefix;
        prefix.reserve(encoded_ns_id.size() + 1 + 8 + 1);
        prefix.append(encoded_ns_id);
        prefix.push_back(B255_DELIMITER);
        prefix.append(EncodeBase255(epoch));
        prefix.push_back(B255_DELIMITER);
        return prefix;
    }

    // 3. 计算用于扫描的排他上限边界 Key (MakePrefixNext)
    inline std::string MakePrefixNext(std::string_view prefix)
    {
        if (prefix.empty())
        {
            return "";
        }
        std::string next_prefix(prefix);
        for (int i = static_cast<int>(next_prefix.size()) - 1; i >= 0; --i)
        {
            auto c = static_cast<unsigned char>(next_prefix[i]);
            if (c != 0xFF)
            {
                next_prefix[i] = static_cast<char>(c + 1);
                next_prefix.resize(i + 1);
                return next_prefix;
            }
        }
        return "";
    }
}
```

---

## 2. 命名空间持久化与数据字典 (Persistence & Metadata Schema)

命名空间的注册与解析数据由专用的内部系统表 `__namespace` 承载，该表同样支持多版本并发控制与事务安全。

### 2.1 元数据键值映射
在 `__namespace` 系统表中维护着四类核心键值对：
1. **`n:<ns_name>` -> `token`**
   - 命名空间名称到认证口令（token）的映射，用于唯一性检验及获取口令。
2. **`t:<token>` -> `encoded_id`**
   - 认证口令到编码后的 Namespace ID 的映射，用于认证时快速定位 ID。
3. **`i:<encoded_id>` -> `ns_name`**
   - 编码后的 Namespace ID 反查命名空间名称，用于 `current` 信息展示。
4. **`next_id` -> `next_seq_integer`**
   - 用于生成自增命名空间数字 ID 的全局计数器。

### 2.2 事务安全操作与级联删除 (Transactional Safety & Cascade Deletion)
`RedisServiceImpl` 提供了全套的事务安全管理接口，包括添加、修改、删除和扫描列表。在删除与清理操作中实现了**级联数据清空与隔离清理**：
- **元数据删除**：首先从 `__namespace` 系统表中删除口令、ID以及名称的双向映射记录。
- **级联 Key 数据清空**：自定义命名空间删除时，只需针对共享的 `ns_data_table` 单个物理表进行范围扫描（边界限定在 `[ns_prefix, ns_prefix_next)` 内，即以该命名空间编码 ID 为前缀的所有租户 Key），并将扫描到的记录批量在同一个事务内全部删除，实现彻底的数据原子级级联清空。
- **租户级独立清理 (`FLUSHDB` / `FLUSHALL`) 与异步 GC 流程**：对于自定义命名空间，`FLUSHDB` 和 `FLUSHALL` 命令被拦截为**逻辑删除**，而非同步 Truncate 物理表，从而避免阻塞主处理流程。
  - **工作流与序列图**：
    
    ```mermaid
    sequenceDiagram
        autonumber
        actor Client
        participant ConnectionContext as Connection Context (in-memory)
        participant ExecuteFlushDB as ExecuteFlushDBCommand
        participant DB as Database (__namespace Table & ns_data_table)
        participant GCDaemon as NamespaceGCDaemon (Background Thread)

        %% Phase 1: Logical Flush
        Client->>ExecuteFlushDB: "Send FLUSHDB / FLUSHALL (Custom Namespace)"
        Note over ExecuteFlushDB: Read old_epoch from ns_meta
        ExecuteFlushDB->>DB: "[Tx 1] Set 'e:<encoded_ns_id>' = new_epoch (old_epoch + 1)"
        ExecuteFlushDB->>DB: "[Tx 1] Set 'g:<encoded_ns_id>:<old_epoch>' = '1' (GC Record)"
        DB-->>ExecuteFlushDB: "[Tx 1] Commit Success"
        ExecuteFlushDB->>ConnectionContext: "Update memory epoch cache (release fence)"
        ExecuteFlushDB-->>Client: "Return 'OK' (Logical Delete Complete)"

        %% Phase 2: Asynchronous GC
        loop Regular Interval (bthread_usleep)
            GCDaemon->>DB: "[Scan Tx] Scan GC records matching 'g:*' range"
            DB-->>GCDaemon: "Return GC records list"
            alt GC records not empty
                loop For each record (g:<ns_id>:<old_epoch>)
                    Note over GCDaemon: Resolve prefix = MakePrefix(ns_id, old_epoch)
                    loop Batch Scan and Delete
                        GCDaemon->>DB: "[Scan Tx] Scan keys with prefix"
                        DB-->>GCDaemon: "Return batch of keys"
                        GCDaemon->>DB: "[Write Tx] Delete batch of keys in ns_data_table"
                        DB-->>GCDaemon: "Commit Success"
                        Note over GCDaemon: Throttling sleep (5ms)
                    end
                    GCDaemon->>DB: "[Write Tx] Delete GC record 'g:<ns_id>:<old_epoch>'"
                    DB-->>GCDaemon: "Commit Success"
                end
            end
        end
    ```
- **对象生命周期延伸**：为避免悬空指针（UAF）风险，所有参与事务的 `EloqKey`、`Command` 及 `TxRequest`（包括大批量级联删除或 FLUSHDB 批量删除创建的临时对象）其生命周期均通过容器进行严格的生命周期延伸绑定，保证在 `CommitTx` 完成前绝对不被析构。

---

## 3. 会话管理与权限控制 (Session & Access Control)

### 3.1 权限与上下文绑定
- **基于 AUTH 的上下文绑定**：
  - 客户端通过 `AUTH <password>` 进行身份认证。
  - 若 `password` 匹配命名空间表中的 `token`，该连接上下文的 `ctx->ns` 和 `ctx->ns_id` 将被分别绑定为对应的命名空间名称及其编码后的 ID，后续此连接发起的所有操作均透明地路由至该命名空间。
  - 若匹配系统全局 `requirepass`，则绑定至 `default` 命名空间。
- **命令与管理权限限制**：
  - **禁止 SELECT 命令**：为确保租户数据安全并简化物理管理，自定义命名空间内的客户端禁止执行 `SELECT` 命令切换数据库。若尝试执行将直接拒绝并返回特定的错误。
  - **管理权限收敛**：只有处于 `default` 命名空间（即使用 `requirepass` 登录）的客户端，才允许执行 `namespace add/get/del/refresh` 等管理操作。
  - 处于自定义命名空间的客户端执行管理子命令时会被拒绝，返回特定错误。
  - 当启用集群模式 (`FLAGS_cluster_mode`) 或 `requirepass` 为空时，禁止所有的命名空间管理操作。

### 3.2 命名空间管理指令说明 (Namespace Control Commands)

可以通过 Redis 协议向服务端发送 `NAMESPACE` 命令来进行命名空间管理，具体指令说明如下表所示：

| 命令格式 | 功能说明 | 权限要求 | 返回值 |
| :--- | :--- | :--- | :--- |
| `NAMESPACE CURRENT` | 获取当前客户端连接绑定的命名空间名称。 | 任意已认证客户端 | 字符串（如 `default` 或自定义命名空间名称） |
| `NAMESPACE GET <ns>` | 查询指定命名空间的认证 Token。 | 仅限 `default` 空间管理员 | 字符串（该空间的 Token）或返回 `ERR namespace not found` |
| `NAMESPACE GET *` | 列出系统中所有的自定义命名空间及其对应的 Token（扁平化数组）。 | 仅限 `default` 空间管理员 | 扁平化数组（如 `[ns1, token1, ns2, token2]`） |
| `NAMESPACE ADD <ns>` | 创建一个新的命名空间。系统会自动生成一个 Base64Url 格式的 Token。 | 仅限 `default` 空间管理员 | 字符串（新生成的 Token）或返回 `ERR the namespace already exists` |
| `NAMESPACE REFRESH <ns>` | 为已存在的命名空间重新生成并更新 Token。 | 仅限 `default` 空间管理员 | 字符串（新生成的 Token）或返回 `ERR namespace not found` |
| `NAMESPACE DEL <ns>` | 删除指定的命名空间及其所有映射，并且会自动级联清空删除该命名空间下的所有用户 Key 数据。 | 仅限 `default` 空间管理员 | 状态字符串 `OK` 或返回 `ERR namespace not found` |

---

## 4. 辅助防崩溃设计 (Crash Prevention Design)

- **`GetDbIndex` 安全防护**：
  - 由于新引入了不带数据库索引后缀的系统表 `__namespace`，子模块 `data_substrate` 中的 `GetDbIndex` 对表名进行安全防空和非数字后缀过滤。若表名为空或不以数字结尾，直接返回 `0`，避免对字符进行越界越权减法而造成 assertion 崩溃。

---

## 5. 测试验证与覆盖说明 (Test Verification & Coverage)

系统的正确性、安全隔离边界以及向下兼容性通过 Javascript 集成测试进行保证。

### 5.1 客户端协议集成测试 ([namespace.test.js](./js/namespace.test.js))
集成测试模拟真实客户端，校验 Redis 协议交互及租户命令隔离权限：
- **子指令全覆盖**：测试全部的命名空间管理命令（`NAMESPACE CURRENT/ADD/GET/REFRESH/DEL`）。
- **数据物理隔离**：验证两个客户端分别在 `default` 和租户命名空间下操作同名的 `shared_key`，默认空间下对应的底层 Key 为无前缀的 `shared_key`，而租户空间下对应的底层 Key 带有其编码后的前缀（如 `\x02\x00shared_key`），互不干扰、独立读写。
- **越权防御**：验证非管理员租户尝试调用 `NAMESPACE ADD` 或 `NAMESPACE GET *` 管理指令时，是否能被系统正确拒绝并返回相应权限错误信息。
- **级联删除校验**：通过在租户空间下写入多个 Key，然后在默认空间下执行 `NAMESPACE DEL` 级联删除该空间，最后验证系统全局 `DBSIZE` 是否精确减少了写入的租户 Key 数量。
