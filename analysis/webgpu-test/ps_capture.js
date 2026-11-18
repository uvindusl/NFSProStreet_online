const { chromium } = require("/home/uvindu/Projects/nfsprostree-recompile/web/node_modules/playwright-core");
(async () => {
  const b = await chromium.launch({ executablePath: "/usr/bin/chromium", headless: true,
    args: ["--enable-unsafe-webgpu","--use-gl=angle","--use-angle=vulkan","--enable-features=Vulkan","--no-sandbox"] });
  const p = await b.newPage();
  const msgs = [];
  p.on("console", m => { 
    msgs.push(m.type()+": "+m.text());
    console.log("CONSOLE:", m.type(), m.text());
  });
  p.on("pageerror", e => { 
    msgs.push("pageerror: "+e.message);
    console.log("PAGEERROR:", e.message);
  });
  await p.goto("file:///home/uvindu/Projects/nfsprostree-recompile/analysis/webgpu-test/ps_shader.html", { waitUntil: "load" });
  
  // Wait for RENDER_COMPLETE message
  await p.waitForFunction(() => {
    const logEl = document.getElementById('log');
    return logEl && logEl.textContent.includes('RENDER_COMPLETE');
  }, { timeout: 60000 });
  
  await p.waitForTimeout(2000);
  
  // Read the log element content
  const logContent = await p.evaluate(() => {
    const logEl = document.getElementById('log');
    return logEl ? logEl.textContent : 'NO LOG ELEMENT';
  });
  console.log("LOG CONTENT:", logContent);
  
  const probe = await p.evaluate(async () => {
    if (!navigator.gpu) return { hasGpu: false };
    const a = await navigator.gpu.requestAdapter();
    if (!a) return { hasGpu: true, adapter: null };
    const d = await a.requestDevice();
    return { hasGpu: true, adapter: a.info, device: !!d };
  });
  console.log("PROBE:", JSON.stringify(probe));
  await p.screenshot({ path: "/home/uvindu/Projects/nfsprostree-recompile/analysis/webgpu-test/ps_shader.png" });
  console.log("PAGELOG:", msgs.join(" | "));
  await b.close();
})().catch(e => { console.error("ERR", e.message); process.exit(1); });