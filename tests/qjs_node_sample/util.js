// A second module, so the staging follows a relative import and rewrites node: imports in more than one file.
import { createHash } from 'node:crypto';
import { join } from 'node:path';
export const digest = (bytes) => createHash('sha256').update(bytes).digest('hex').slice(0, 16);
export const where = (dir, name) => join(dir, name);
