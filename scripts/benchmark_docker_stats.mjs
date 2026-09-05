export async function readDockerStats(execFileAsync, { timeoutMs = 2000 } = {}) {
  try {
    const { stdout } = await execFileAsync('docker', [
      'stats', '--no-stream', '--format', '{{.Name}}\\t{{.CPUPerc}}\\t{{.MemUsage}}\\t{{.MemPerc}}',
    ], { windowsHide: true, maxBuffer: 1024 * 1024, timeout: timeoutMs });
    return stdout.trim().split(/\r?\n/).filter(Boolean).map(line => {
      const [name, cpu, memory, memoryPercent] = line.split('\t');
      return { name, cpuPercent: parseMetricNumber(cpu), memory, memoryPercent: parseMetricNumber(memoryPercent) };
    });
  } catch {
    return [];
  }
}

function parseMetricNumber(value) {
  const match = String(value).replace(',', '.').match(/[0-9]+(?:\.[0-9]+)?/);
  return match ? Number(match[0]) : null;
}
