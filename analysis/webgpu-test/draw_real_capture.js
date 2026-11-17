const { chromium } = require(__dirname + "/../../web/node_modules/playwright-core");
const path = require("path");
const HERE = __dirname;
const PAGE = "file://" + path.join(HERE, "draw_real.html");
const OUT = path.join(HERE, "draw_real.png");
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
  await p.goto(PAGE, { waitUntil: "load" });

  // Wait for the DRAW_REAL summary line
  await p.waitForFunction(() => {
    const logEl = document.getElementById('log');
    return logEl && logEl.textContent.includes('DRAW_REAL');
  }, { timeout: 120000 });

  await p.waitForTimeout(1000);

  const logContent = await p.evaluate(() => {
    const logEl = document.getElementById('log');
    return logEl ? logEl.textContent : 'NO LOG ELEMENT';
  });
  console.log("=== DRAW REAL LOG ===");
  console.log(logContent);
  console.log("=== END DRAW REAL LOG ===");

  // Screenshot the rendered canvas page
  await p.screenshot({ path: OUT });
  console.log("PNG saved: " + OUT);
  console.log("PAGELOG:", msgs.join(" | "));
  await b.close();
})().catch(e => { console.error("ERR", e.message); process.exit(1); });
