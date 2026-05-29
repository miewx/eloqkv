# EloqKV 命名空间设计与核心架构

本文件详述了修改后的 EloqKV C++ 命名空间隔离与管理机制的核心设计架构。所有设计均围绕代码的实际差异和重构后的全新命名空间逻辑展开。

---

## 0. 兼容性与配置设计 (Compatibility & Configuration)

### 0.1 向下兼容与默认行为 (Default Prefixless Behavior)
默认情况下，本版本移除了对 `namespace` 显式开关配置的要求，**命名空间隔离机制默认始终启用**：
- **默认命名空间（`default`）**：默认数据是无前缀的（prefixless），即直接路由到原有的物理数据库表（如 `data_table_0`, `data_table_1` 等），行为与没有命名空间的原版逻辑完全一致。
- **自定义命名空间**：通过 `AUTH <token>` 认证的客户端，数据会自动路由到共享的物理表 `ns_data_table`，并在内部采用独特的 Base-255 编码前缀实现前缀隔离。这避免了为每个命名空间创建物理表的系统开销。

### 0.2 历史兼容切换配置 (Legacy Compatibility Mode)
为保持对历史版本的完全兼容（在历史版本中，`default` 命名空间也带前缀 `\x01\x00`）：
- 如果在配置文件 `eloqkv.ini` 的 `[local]` 段下显式设置 `namespace = true`，系统将启用**历史兼容模式**（即 `use_legacy_default_ns = true`）。
- 在历史兼容模式下，默认命名空间的数据也会透明加上 `\x01\x00` 的前缀，从而与旧版数据保持一致。

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
  - **默认命名空间（`default`）**：默认情况下，默认命名空间的数据是**无前缀的（prefixless）**，保持原生的 Key 编码。其数据单独路由到原有的物理数据库表（如 `data_table_0`, `data_table_1` 等）。
    - *向下兼容*：如果在配置文件中设置了 `use_legacy_default_ns = true`，则默认命名空间将回退到旧版带 `\x01\x00` 前缀 of 编码格式。
  - **自定义命名空间**：所有自定义命名空间共享单个物理表 `ns_data_table`。
    - 各自定义命名空间在 `ns_data_table` 内使用独特的 Base-255 编码前缀实现前缀隔离。
    - 每个自定义命名空间都有唯一的 `uint64_t` 标识，前缀格式为 `[encoded_id] + \x00`。
    - 由于 `\x00` 仅作为前缀分隔符，且 `EncodeBase255` 编码中排除 `\x00`，因此保证了各个命名空间 Key 之间的无碰撞与安全隔离。
- **透明包装**：
  - `EloqKey` 在构造时通过 `CreateEloqStringFromNamespace` 透明地加上当前的命名空间前缀（对于默认命名空间，当 `use_legacy_default_ns` 为 `false` 时不加前缀）。
- **范围限制 (`ComposeNamespaceKeyNext`)**：
  - 针对 `KEYS` / `SCAN` 范围查找，通过将起始 Key 定位为 `[encoded_id] + \x00`，结束 Key 定位为 `ComposeNamespaceKeyNext`（即 `[encoded_id] + \x01`），将检索边界严格限定在当前命名空间范围内。

---

## 2. 命名空间持久化与数据字典 (Persistence & Metadata Schema)

命名空间的注册 and 解析数据由专用的内部系统表 `__namespace` 承载，该表同样支持多版本并发控制与事务安全。

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
- **租户级独立清理 (`FLUSHDB` / `FLUSHALL`)**：对于自定义命名空间，`FLUSHDB` 和 `FLUSHALL` 命令被拦截为逻辑清理，而非直接 Truncate 物理表。系统同样在 `ns_data_table` 表内进行前缀范围扫描，批量删除该命名空间下的所有 Key，并利用事务级元数据计数器重置 Key 计数，从而保证了清理操作对其他租户完全透明且互不干扰。
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
| `NAMESPACE GET *` | 列出系统中所有的命名空间及其对应的 Token（扁平化数组）。 | 仅限 `default` 空间管理员 | 数组（如 `[ns1, token1, ns2, token2, default, requirepass]` |
| `NAMESPACE ADD <ns>` | 创建一个新的命名空间。系统会自动生成一个 Base64Url 格式的 Token。 | 仅限 `default` 空间管理员 | 字符串（新生成的 Token）或返回 `ERR the namespace already exists` |
| `NAMESPACE REFRESH <ns>` | 为已存在的命名空间重新生成并更新 Token。 | 仅限 `default` 空间管理员 | 字符串（新生成的 Token）或返回 `ERR namespace not found` |
| `NAMESPACE DEL <ns>` | 删除指定的命名空间及其所有映射，并且会自动级联清空删除该命名空间下的所有用户 Key 数据。 | 仅限 `default` 空间管理员 | 状态字符串 `OK` 或返回 `ERR namespace not found` |

---

## 4. 辅助防崩溃设计 (Crash Prevention Design)

- **`GetDbIndex` 安全防护**：
  - 由于新引入了不带数据库索引后缀的系统表 `__namespace`，子模块 `data_substrate` 中的 `GetDbIndex` 对表名进行安全防空和非数字后缀过滤。若表名为空或不以数字结尾，直接返回 `0`，避免对字符进行越界越权减法而造成 assertion 崩溃。

---

## 5. 测试验证与覆盖说明 (Test Verification & Coverage)

系统的正确性、安全隔离边界以及向下兼容性通过 C++ 单元测试与 Javascript 集成测试进行双重保证。

### 5.1 C++ 单元测试 ([namespace_test.cpp](./tests/unit/eloq/namespace_test.cpp))
单元测试主要验证在不同配置模式下，底层内存模型及键前缀包装的安全边界。
- **管理器功能 (`TestNamespaceManager`)**：验证内存缓存模式下的 `Add`, `Set`, `Del`, `GetByToken`, `List` 操作，测试唯一性限制。
- **兼容性验证 (`TestNamespacePrefixing` - CASE 1)**：
  - 当 `enable_namespace` 为 `false` 时，验证自定义、默认和空命名空间下的 Key 操作**完全不附加任何前缀**（`ApplyNamespace` 保持原样返回原键），`ComposeNamespaceKeyNext` 返回空，保证不破坏原有的非隔离系统数据结构。
- **隔离模式验证 (`TestNamespacePrefixing` - CASE 2)**：
  - 当 `enable_namespace` 为 `true` 时，验证默认空间前缀确为 `\x01\x00`，自定义空间前缀为 `\x02\x00`。
  - 验证 B 树范围扫描 of 辅助边界计算（如 `ComposeNamespaceKeyNext("\x02\x00")` 返回 `\x02\x01`），确立严密的租户检索边界。

### 5.2 客户端协议集成测试 ([namespace.test.js](./js/namespace.test.js))
集成测试模拟真实客户端，校验 Redis 协议交互及租户命令隔离权限：
- **子指令全覆盖**：测试全部的命名空间管理命令（`NAMESPACE CURRENT/ADD/GET/REFRESH/DEL`）。
- **数据物理隔离**：验证两个客户端分别在 `default` 和租户命名空间下操作同名的 `shared_key`，底层分别对应 `\x01\x00shared_key` 和 `\x02\x00shared_key`，互不干扰、独立读写。
- **越权防御**：验证非管理员租户尝试调用 `NAMESPACE ADD` 或 `NAMESPACE GET *` 管理指令时，是否能被系统正确拒绝并返回相应权限错误信息。
- **级联删除校验**：通过在租户空间下写入多个 Key，然后在默认空间下执行 `NAMESPACE DEL` 级联删除该空间，最后验证系统全局 `DBSIZE` 是否精确减少了写入的 Key 数量。
