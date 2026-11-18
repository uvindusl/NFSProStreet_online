const { chromium } = require("/home/uvindu/Projects/nfsprostree-recompile/web/node_modules/playwright-core");
(async () => {
  const b = await chromium.launch({ executablePath: "/usr/bin/chromium", headless: true,
    args: ["--enable-unsafe-webgpu","--use-gl=angle","--use-angle=vulkan","--enable-features=Vulkan","--no-sandbox"] });
  const p = await b.newPage();
  p.setDefaultTimeout(120000);
  const msgs = [];
  p.on("console", m => {
    msgs.push("CONSOLE:"+m.type()+": "+m.text());
    console.log("CONSOLE:", m.type(), m.text());
  });
  p.on("pageerror", e => {
    msgs.push("pageerror: "+e.message);
    console.log("PAGEERROR:", e.message);
  });
  await p.goto("file:///home/uvindu/Projects/nfsprostree-recompile/analysis/webgpu-test/encoder-smoke.html", { waitUntil: "load" });

  await p.waitForFunction(() => {
    const logEl = document.getElementById('log');
    return logEl && logEl.textContent.includes('SMOKE_DONE');
  }, { timeout: 120000 });

  await p.waitForTimeout(1000);

  const logContent = await p.evaluate(() => {
    const logEl = document.getElementById('log');
    return logEl ? logEl.textContent : 'NO LOG ELEMENT';
  });
  console.log("=== ENCODER SMOKE LOG ===");
  console.log(logContent);
  console.log("=== END ENCODER SMOKE LOG ===");

  await p.screenshot({ path: "/home/uvindu/Projects/nfsprostree-recompile/analysis/webgpu-test/encoder-smoke.png" });
  console.log("PNG saved: analysis/webgpu-test/encoder-smoke.png");
  console.log("PAGELOG:", msgs.join(" | "));
  await b.close();
})().catch(e => { console.error("ERR", e.message); process.exit(1); });
