# 分片上传压测报告

> 实测时间：2026-08-13T04:29:23.828Z
> 环境：Docker Desktop + 本机 HTTPS 回环；绝对吞吐不代表线上容量，仅比较同机、同文件集、同 worker 配置下的相对结果。

## 工作负载

- 每轮文件：10 MiB × 96 分片 = 960 MiB
- 每组：2 轮；固定并发为 8，AIMD 为 4→32
- 后端：chunk_upload 默认 8 workers；真实接口：/api/chunk_init、/api/chunk_upload、/api/chunk_merge。

## 核心结果（各组中位数）

| 指标 | 固定并发 8 | AIMD 4→32 |
| --- | ---: | ---: |
| throughputMiBps | 72.35 | 81.91 |
| durationMs | 11046.06 | 11189.12 |
| p50ChunkRttMs | 647.09 | 2220.14 |
| p95ChunkRttMs | 966.16 | 3139.93 |
| p99ChunkRttMs | 1170.6 | 3209.78 |
| retries | 0 | 0 |
| timeouts | 0 | 0 |
| retransmissionRatePct | 0 | 0 |
| maxInFlight | 8 | 32 |
| peakWindow | 8 | 32 |

## 对比结论

- AIMD 吞吐相对固定并发：**+13.21%**。
- 超时驱动重传变化：**N/A (both 0%)**。
- 本轮实际峰值窗口：32；实际最大在途请求：32。

## 容器峰值资源

| 容器 | 固定 CPU% | 固定内存 MiB | AIMD CPU% | AIMD 内存 MiB |
| --- | ---: | ---: | ---: | ---: |
| tc_fcgi_app | 135.18 | 151.3 | 158.77 | 154.1 |
| tc_fcgi_nginx_fastdfs | 115.06 | 81.22 | 125.16 | 99.37 |
| tc_fcgi_mysql | 3.74 | 459.1 | 10.13 | 461.9 |

## 可复现命令

```powershell
node scripts/benchmark_chunk_upload.mjs --trials 2 --chunks 96 --chunk-mib 10
```

完整逐轮数据见同名 JSON 文件。
