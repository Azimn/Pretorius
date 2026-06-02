const fs = require('fs');
const path = require('path');

function resolveHost(projectRoot) {
  const host = path.join(projectRoot, 'build', 'persona_host');
  if (fs.existsSync(host)) return host;
  if (fs.existsSync(`${host}.exe`)) return `${host}.exe`;
  return host;
}

module.exports = { resolveHost };
