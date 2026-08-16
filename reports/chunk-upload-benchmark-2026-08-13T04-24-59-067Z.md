# 分片上传压测报告

> 实测时间：2026-08-13T04:24:59.067Z
> 环境：Docker Desktop + 本机 HTTPS 回环；绝对吞吐不代表线上容量，仅比较同机、同文件集、同 worker 配置下的相对结果。

## 工作负载

- 每轮文件：10 MiB × 24 分片 = 240 MiB
- 每组：3 轮；固定并发为 8，AIMD 为 4→32
- 后端：chunk_upload 默认 8 workers；真实接口：/api/chunk_init、/api/chunk_upload、/api/chunk_merge。

## 核心结果（各组中位数）

| 指标 | 固定并发 8 | AIMD 4→32 |
| --- | ---: | ---: |
| throughputMiBps | 109.52 | 59.5 |
| durationMs | 2191.31 | 4033.74 |
| p50ChunkRttMs | 531.49 | 628.67 |
| p95ChunkRttMs | 773.75 | 889.27 |
| p99ChunkRttMs | 802.69 | 908.91 |
| retries | 0 | 0 |
| timeouts | 0 | 0 |
| retransmissionRatePct | 0 | 0 |
| maxInFlight | 8 | 13 |
| peakWindow | 8 | 13 |

## 对比结论

- AIMD 吞吐相对固定并发：**-45.67%**。
- 超时驱动重传变化：**N/A (both 0%)**。
- 本轮实际峰值窗口：13；实际最大在途请求：13。

## 容器峰值资源

| 容器 | 固定 CPU% | 固定内存 MiB | AIMD CPU% | AIMD 内存 MiB |
| --- | ---: | ---: | ---: | ---: |
| tc_fcgi_app | 55.85 | 130 | 97.18 | 129.5 |
| tc_fcgi_nginx_fastdfs | 99.58 | 417 | 104.81 | 438.3 |
| tc_fcgi_mysql | 4.68 | 454.1 | 5.13 | 454.5 |

## 可复现命令

```powershell
node scripts/benchmark_chunk_upload.mjs --trials 3 --chunks 24 --chunk-mib 10
```

完整逐轮数据见同名 JSON 文件。
