# 分片上传压测报告

> 实测时间：2026-08-13T04:27:53.283Z
> 环境：Docker Desktop + 本机 HTTPS 回环；绝对吞吐不代表线上容量，仅比较同机、同文件集、同 worker 配置下的相对结果。

## 工作负载

- 每轮文件：10 MiB × 24 分片 = 240 MiB
- 每组：3 轮；固定并发为 8，AIMD 为 4→32
- 后端：chunk_upload 默认 8 workers；真实接口：/api/chunk_init、/api/chunk_upload、/api/chunk_merge。

## 核心结果（各组中位数）

| 指标 | 固定并发 8 | AIMD 4→32 |
| --- | ---: | ---: |
| throughputMiBps | 129.67 | 128.15 |
| durationMs | 1850.82 | 1872.77 |
| p50ChunkRttMs | 581.12 | 689.52 |
| p95ChunkRttMs | 745.2 | 796.19 |
| p99ChunkRttMs | 772.46 | 814.79 |
| retries | 0 | 0 |
| timeouts | 0 | 0 |
| retransmissionRatePct | 0 | 0 |
| maxInFlight | 8 | 14 |
| peakWindow | 8 | 14 |

## 对比结论

- AIMD 吞吐相对固定并发：**-1.17%**。
- 超时驱动重传变化：**N/A (both 0%)**。
- 本轮实际峰值窗口：14；实际最大在途请求：14。

## 容器峰值资源

| 容器 | 固定 CPU% | 固定内存 MiB | AIMD CPU% | AIMD 内存 MiB |
| --- | ---: | ---: | ---: | ---: |
| tc_fcgi_app | 76.74 | 115.3 | 79.79 | 115.3 |
| tc_fcgi_nginx_fastdfs | 62.67 | 26.79 | 77.3 | 27.19 |
| tc_fcgi_mysql | 4.61 | 454.9 | 6.05 | 454.8 |

## 可复现命令

```powershell
node scripts/benchmark_chunk_upload.mjs --trials 3 --chunks 24 --chunk-mib 10
```

完整逐轮数据见同名 JSON 文件。
