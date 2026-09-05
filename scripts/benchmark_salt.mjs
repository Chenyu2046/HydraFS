import crypto from 'node:crypto';

export function deriveBenchmarkRoundSalt(baseSalt, round, sizeMiB) {
  const digest = crypto.createHash('sha256')
    .update(`${baseSalt}:${round}:${sizeMiB}`)
    .digest();
  return digest.readUInt32LE(0);
}
