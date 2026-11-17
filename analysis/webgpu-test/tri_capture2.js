const { chromium } = require("/home/uvindu/Projects/nfsprostree-recompile/web/node_modules/playwright-core");
(async () => {
  const b = await chromium.launch({ executablePath: "/usr/bin/chromium", headless: true,
    args: ["--enable-unsafe-webgpu","--use-gl=angle","--use-angle=vulkan","--enable-features=Vulkan","--no-sandbox"] });
  const p = await b.newPage();
  const msgs = [];
  p.on("console", m => msgs.push(m.type()+": "+m.text()));
  p.on("pageerror", e => msgs.push("pageerror: "+e.message));
  // Check navigator.gpu on about:blank FIRST
  const blankInfo = await p.evaluate(async () => {
    if (!navigator.gpu) return { hasGpu: false };
    const a = await navigator.gpu.requestAdapter();
    if (!a) return { hasGpu: true, adapter: null };
    const d = await a.requestDevice();
    return { hasGpu: true, adapter: a.info, device: !!d };
  });
  console.log("ABOUT_BLANK:", JSON.stringify(blankInfo));
  // Now load the triangle page
  await p.goto("file:///home/uvindu/Projects/nfsprostree-recompile/analysis/webgpu-test/triangle.html", { waitUntil: "load" });
  await p.waitForTimeout(2500);
  const pageInfo = await p.evaluate(async () => {
    if (!navigator.gpu) return { hasGpu: false };
    const a = await navigator.gpu.requestAdapter();
    if (!a) return { hasGpu: true, adapter: null };
    const d = await a.requestDevice();
    return { hasGpu: true, adapter: a.info, device: !!d };
  });
  console.log("PAGE:", JSON.stringify(pageInfo));
  await p.screenshot({ path: "/home/uvindu/Projects/nfsprostree-recompile/analysis/webgpu-test/tri.png" });
  console.log("PAGELOG:", msgs.join(" | "));
  await b.close();
})().catch(e => { console.error("ERR", e.message); process.exit(1); });