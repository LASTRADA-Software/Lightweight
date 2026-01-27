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

// List of HTML files to transform
const htmlFiles = ['index.html', 'sql-migrations.html'];

for (const file of htmlFiles) {
  const srcPath = path.join(__dirname, file);
  if (fs.existsSync(srcPath)) {
    // Read and transform HTML
    let html = fs.readFileSync(srcPath, 'utf8');

    // Update paths from node_modules/reveal.js to reveal.js
    html = html.replace(/node_modules\/reveal\.js/g, 'reveal.js');

    // Write transformed HTML
    fs.writeFileSync(path.join(distDir, file), html);
    console.log(`Built: ${file}`);
  }
}

console.log('Build complete! Output in dist/');
