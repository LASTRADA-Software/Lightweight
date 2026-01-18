const fs = require('fs');
const path = require('path');

const distDir = path.join(__dirname, 'dist');
const nodeModules = path.join(__dirname, 'node_modules');
const revealDir = path.join(nodeModules, 'reveal.js');

// Create dist directory
if (!fs.existsSync(distDir)) {
  fs.mkdirSync(distDir, { recursive: true });
}

// Copy reveal.js dist folder
function copyDir(src, dest) {
  if (!fs.existsSync(dest)) {
    fs.mkdirSync(dest, { recursive: true });
  }
  const entries = fs.readdirSync(src, { withFileTypes: true });
  for (const entry of entries) {
    const srcPath = path.join(src, entry.name);
    const destPath = path.join(dest, entry.name);
    if (entry.isDirectory()) {
      copyDir(srcPath, destPath);
    } else {
      fs.copyFileSync(srcPath, destPath);
    }
  }
}

// Copy reveal.js assets
copyDir(path.join(revealDir, 'dist'), path.join(distDir, 'reveal.js', 'dist'));
copyDir(path.join(revealDir, 'plugin'), path.join(distDir, 'reveal.js', 'plugin'));

// Read and transform index.html
let html = fs.readFileSync(path.join(__dirname, 'index.html'), 'utf8');

// Update paths from node_modules/reveal.js to reveal.js
html = html.replace(/node_modules\/reveal\.js/g, 'reveal.js');

// Write transformed index.html
fs.writeFileSync(path.join(distDir, 'index.html'), html);

console.log('Build complete! Output in dist/');
