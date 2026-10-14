# 音频流与生命周期

[English](gcores-audio.md)

播放器将压缩音频写入有界 FIFO，在 ESP32-C3 上解码，再把单声道 PCM 送入 ES8311。
它不下载完整节目，也不在 RAM 中维护不断增长的跳转索引。C3 没有 PSRAM，音频、
TLS、Wi-Fi 和 UI 共用内部 SRAM。整体预算见 [内存架构](gcores-memory.zh_CN.md)。

## 所有权与 API 契约

[`passport_audio.h`](../../main/passport_audio.h) 是应用接口。生命周期任务应在命令
生产者启动前初始化一次。HTTP 任务负责传输，只有解码任务能访问音频 BSP。
任务提供复制后的状态快照，不保留 UI 对象或回调。

`play`、`pause`、`stop`、`seek`、`set_volume` 提交命令后立即返回，不等待操作完成。
队列有八个槽位，满时返回 `ESP_ERR_TIMEOUT`。`play` 校验 HTTP/HTTPS 协议并复制
最长 1023 字节的 URL，返回后调用者可释放输入。音量限制为 0–100%。命令被接受
不代表播放成功，应通过 `passport_audio_get_status()` 查询状态和错误。

状态包括 `IDLE`、`BUFFERING`、`PLAYING`、`PAUSED`、`ENDED`、`ERROR`。提交 `stop`
后，应等 `passport_audio_is_quiescent()` 为 true，再进入系统休眠或执行依赖音频
资源空闲的目录网络请求。暂停仍保留解码器、缓冲和网络请求，不属于 quiescent。
外部 BSP 休眠、唤醒、格式切换操作不得与音频子系统并发。

## 支持的媒体

| 媒体 | 支持的表示 |
|---|---|
| MP3 | MPEG Layer III，含 CBR、VBR |
| AAC | ADTS AAC-LC |
| M4A | AAC-LC 或 MP3，要求 `moov` 在 `mdat` 前（faststart） |
| WAV | 16 位 PCM |

解码输出必须为 8–48 kHz、16 位、单声道或双声道 PCM。双声道通过 32 位算术求
平均转为单声道，保留源采样率；同一流中改变采样率会被拒绝。不支持 HE-AAC/SBR、
ALAC、Opus/Ogg、FLAC、HLS 和加密媒体。只向前读取的解析器不能处理非 faststart M4A。

依赖为 Espressif `esp_audio_codec` 2.6.2，保留所需 ESP32-C3 静态库和头文件。
其 [修改版 MIT 许可](../../components/esp_audio_codec/LICENSE) 限制用于 Espressif
产品。供应商解码器分配同样计入 SRAM 预算。

## 有界资源

| 资源 | 分配或上限 |
|---|---|
| HTTP 任务栈 | 8192 字节，常驻 |
| 解码任务栈 | 12288 字节，常驻 |
| 压缩数据 FIFO | 12 KiB，常驻 |
| 首次流预填充 | 8 KiB；提前 EOF 或错误时不再等待 |
| 解码输入 | 3072 字节，播放时分配 |
| PCM 输出 | 初始 4608 字节；最多一次受检扩容至 8192 字节 |
| HTTP scratch | 每次请求 1024 字节，完成或取消后释放 |
| HTTP 客户端 RX / TX | 1024 / 1024 字节，TLS 分配另计 |
| 首次 PCM 前头部扫描 | 最多 256 KiB 压缩输入 |
| MP3 跳转元数据 | 小于 128 字节，无增长式稀疏索引 |

初始 PCM 可容纳 1152 个双声道 16 位 MP3 采样。只有解码器请求更大输出、未消耗
输入且未产出 PCM 时，才允许一次扩容到 8 KiB，再重试同一段输入。超出上限或已
部分消耗输入的歧义情况会失败，避免丢音或重复解码。

打开解码器前，MP3 要求至少 64 KiB 空闲内部堆，AAC、M4A、WAV 要求 80 KiB。
这些是准入检查，不保证供应商分配成功：空闲堆总量不能保证足够大的连续块。
MP3 核心约占 27.2 KiB，解析器、封装、传输和分配器另有开销。活动播放期间应避免
目录 TLS 请求重叠，不能仅凭核心大小降低准入阈值。

## HTTP 身份与恢复

HTTPS 使用 ESP-IDF 证书包验证证书与主机名，系统时间必须有效。公开源仍提供
HTTP 地址，因此允许明文 HTTP。重定向最多五跳，拒绝非 HTTP 协议，以及 HTTPS
降级到 HTTP。请求发送 `Accept-Encoding: identity`。

套接字超时为 3000 ms，最多两次读取恢复。续传身份只接受带双引号的强 ETag，弱
ETag 和 Last-Modified 不能用于续传。续传响应必须同时满足：

- 状态为 206，强 ETag 与原资源一致。
- `Content-Range` 起点等于请求的绝对字节位置，完整资源总长保持不变。
- 开放式 Range 延伸至资源最后一个字节。
- 已知 `Content-Length` 时必须等于 Range 长度。分块响应保持长度未知，并在 EOF
  检查实际交付字节数。

恢复位置等于初始 Range 偏移加实际写入 FIFO 的字节数。只读进 scratch、尚未交付
的字节不能推进偏移。新的 200 响应体、变化的 ETag 或非法 206 响应体不会拼入已有
解码流。实现见 [响应校验器](../../main/passport_audio_http.c)。

取消时改变网络 generation，最多等待 6500 ms。若超时，任务保留 HTTP 任务可能
仍在使用的 URL，并拒绝替换，避免释放被借用的内存。传输失败通过音频状态报告；
任意网络错误都不意味着系统能自动成功重启。

## 暂停与跳转

暂停保留播放意图和流位置，并让 codec 与 I2S 输出休眠。暂停时接受的跳转保持
静音且待完成，直到显式恢复。UI 在按住手势期间只做本地预览，松手后提交一次
最终 `seek`，预览刷新不能持续填充命令队列。

`seeking`、`seek_request_id`、`seek_complete_id` 区分接受请求与目标 PCM 完成。
暂停跳转要等恢复。请求 ID 防止旧操作的 PCM 把较新的跳转提前标记为完成。
终态 `ERROR`、`IDLE`、`ENDED` 结束待完成操作。命令入队成功不等于跳转完成。

### MP3 快速近似跳转

Info/Xing 发现要求首个完整 MPEG 帧位于初始 3 KiB 输入内，因此较大 ID3 标签走
顺序回退。元数据必须匹配完整 HTTP 资源，包括首个 MPEG 字节偏移，以及可选的
末尾 128 字节 ID3v1 标签。Range 路径还要求强 ETag 和已知完整资源大小。

Info 使用 CBR 平均字节映射，Xing 对 100 项 8 位 TOC 插值。Range 从目标前约一秒
开始，有界本地扫描确认三个相邻 MPEG 头部，再按完整帧逐个解码。输出保持静音，
直到储备池和合成状态至少预热三个帧、511 字节 main data。缺数据预热帧只允许在
固定上限内容忍。

`position_approximate` 标识此路径。CBR 估算受帧长、padding、编码器延迟影响。
Xing 一个量化单位约为节目时长 / 256，长播客可达数十秒，编码器 TOC 本身也有
误差。快速跳转不保证采样级精确。时长取自完整元数据，不使用部分 206 响应的
Content-Length；报告位置包含 Range 基准位置和之后解码的采样。

Range 身份、帧同步或解码失败时，取消请求，重置 FIFO、解码器、储备池和时间
基准，再从字节零顺序解码、静音丢弃至目标。实现见
[MP3 跳转代码](../../main/passport_mp3_seek.c)。

### 顺序跳转

没有可信快速跳转元数据，或目标位于当前流位置之后三秒以内时，向前跳转保留
解码器、解析器、FIFO 和 HTTP 游标，通过丢弃采样到达目标。没有可用快速路径的
向后跳转、已结束流或未知游标会从字节零重启。`play(url, resume_seconds)` 也从零
开始解码到保存位置。较长的顺序跳转可能耗时较多，并消耗相应流量。

静音丢弃期间 DAC 保持休眠，每帧检查命令，每次 codec 调用后释放计算锁；计算
满 20 ms 后让出一个调度 tick。静音解码仍有计算功耗。

## BSP 与功耗边界

`bsp_audio_init_playback()` 仅分配 I2S TX，保持六段 DMA、每段 240 帧。省去的初始
双声道 16 位 RX DMA 载荷为 5760 字节，另有驱动开销；这不是实测总堆收益。
全双工 `bsp_audio_init()` 仍可用于采集。正在运行的 TX-only 输出升级返回
`ESP_ERR_INVALID_STATE`；格式和采集切换必须在 PCM I/O 停止后串行执行。

播放请求 ES8311 DAC-only 模式。供应商序列仍可能操作 ADC 和模拟偏置，因此不能
保证所有模拟采集模块都已断电。暂停和停止执行完整 suspend 序列，回读关键寄存器
并停止 I2S。板上没有 MCU 控制的功放使能引脚，固件不能独立切断功放电源。

CPU DFS 为 80–160 MHz。最高频率锁覆盖解码器打开、处理调用，在 FIFO、HTTP、
PCM/DMA 等待前释放。为保留原生 USB，自动 light sleep 关闭。实际空闲、播放和
休眠电流必须用硬件测量；减小任务栈前，也必须在 TLS 重定向、恢复和各类支持
codec 下检查设备栈水位。

## 验证参考

[音频命令与 MP3 测试](../../tools/test_passport_audio.py) 用主机 I/O 替身验证命令
分派、暂停/跳转意图、完成状态和有界 PCM 扩容。
[HTTP 测试](../../tools/test_passport_audio_http.py) 验证生产响应校验器。运行需要
Python 和 C11 主机编译器。主机测试不能验证供应商解码、真实 TLS 栈深、声音输出或
板级电流。设备验收应覆盖支持格式、欠载、暂停恢复、前后跳转、Range 拒绝及回退、
停止/休眠/唤醒，以及重复运行后的资源恢复。
