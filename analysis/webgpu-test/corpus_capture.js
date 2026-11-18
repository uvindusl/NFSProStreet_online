const { chromium } = require("/home/uvindu/Projects/nfsprostree-recompile/web/node_modules/playwright-core");
(async () => {
  const b = await chromium.launch({ executablePath: "/usr/bin/chromium", headless: true,
    args: ["--enable-unsafe-webgpu","--use-gl=angle","--use-angle=vulkan","--enable-features=Vulkan","--no-sandbox"] });
  const p = await b.newPage();
  p.setDefaultTimeout(1200000);
  const msgs = [];
  p.on("console", m => { msgs.push("CONSOLE:"+m.type()+": "+m.text()); });
  p.on("pageerror", e => { msgs.push("PAGEERROR: "+e.message); });
  await p.goto("file:///home/uvindu/Projects/nfsprostree-recompile/analysis/webgpu-test/corpus.html", { waitUntil: "load" });

  // Wait for the CORPUS OK= summary line
  await p.waitForFunction(() => {
    const logEl = document.getElementById('log');
    return logEl && logEl.textContent.includes('CORPUS OK=');
  }, { timeout: 1200000 });

  await p.waitForTimeout(1500);

  const logContent = await p.evaluate(() => {
    const logEl = document.getElementById('log');
    return logEl ? logEl.textContent : 'NO LOG ELEMENT';
  });
  console.log("=== CORPUS LOG ===");
  console.log(logContent);
  console.log("=== END CORPUS LOG ===");
  console.log("PAGELOG:", msgs.join(" | "));
  await b.close();
})().catch(e => { console.error("ERR", e.message); process.exit(1); });
