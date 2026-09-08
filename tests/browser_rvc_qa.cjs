/* Local static dashboard with mocked API responses. Never contacts robot hardware. */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {chromium} = require(process.env.PLAYWRIGHT_MODULE || 'playwright');

(async () => {
  const base = process.env.QA_BASE_URL || 'http://127.0.0.1:8877';
  assert.equal(new URL(base).hostname, '127.0.0.1');
  const output = path.resolve(process.env.QA_OUTPUT_DIR || 'browser-qa-output/rvc');
  fs.mkdirSync(output, {recursive:true});
  const browser = await chromium.launch({channel:'msedge', headless:true});
  try {
    const page = await browser.newPage();
    const errors = [];
    page.on('pageerror', error => errors.push(error.message));
    let state = 'ready';
    const commands=[];
    await page.route('**/api/**', async route => {
      if (route.request().method() === 'POST') {
        assert.equal(new URL(route.request().url()).pathname,'/api/navigation');
        commands.push(route.request().postDataJSON());
        return route.fulfill({json:{sent:'F 1',acknowledged:false}});
      }
      assert.equal(route.request().method(), 'GET');
      if (state === 'offline') return route.abort();
      await route.fulfill({json:{mode:state === 'integrated' ? 'live' : 'simulated', bridge:{serial_connected:true,serial_port:'SIMULATED'},
        capture:{state:'idle',written:0,dropped:0}, observed:{
          profile:{id:'unknown',name:'Maker UART-RVC diagnostic',encoder_wheels:[],imu_transport:state === 'integrated' ? 'uart-rvc' : null},
          samples:[],events:[],diagnostics:{},imu:state === 'integrated' ? {
            valid:true,fresh:true,age_s:0.1,transport:'uart-rvc',yaw_rad:0.5,control_ready:true,
            bad_checksum:0,discontinuities:0,uart_errors:0
          } : null,rvc:state === 'integrated' ? null : {valid:state === 'ready',reason:state,
            run:{fresh:state === 'ready',new:100,window_ms:1000},
            value:{fresh:state === 'ready',ypr_deg:[-.01,-.75,.4],age_s:.1},
            counts:{fresh:state === 'ready',bad_checksum:0,discontinuities:0},
            uart:{fresh:state === 'ready',totals:[0,0,0,0,0]}}
        }}});
    });
    for (const width of [1440,390]) {
      state='ready';
      await page.setViewportSize({width,height:1000});
      await page.goto(base+'/ops.html');
      await page.waitForFunction(() => document.querySelector('#imuState').textContent.includes('RVC yaw'));
      assert.match(await page.locator('#rvcDetail').textContent(), /100 frames\/s/);
      assert.equal(await page.locator('#resetPose').isDisabled(),true);
      assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth+1),true);
      await page.screenshot({path:path.join(output,`rvc-${width}.png`),fullPage:true});
    }
    state='stale';
    await page.waitForFunction(() => document.querySelector('#imuState').textContent === 'RVC · stale');
    state='integrated';
    await page.waitForFunction(() => document.querySelector('#rvcDetail').textContent.includes('accepted for this session'));
    for (const width of [1440,390]) {
      await page.setViewportSize({width,height:1000});
      assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth+1),true);
      await page.screenshot({path:path.join(output,`integrated-rvc-${width}.png`),fullPage:true});
    }
    await page.locator('#navAccept').click();
    assert.equal(commands.length,0);
    await page.locator('#headingMeasured').check();
    await page.locator('#navAccept').click();
    await page.waitForFunction(() => document.querySelector('#notice').textContent.includes('not yet verified'));
    assert.deepEqual(commands,[{action:'accept',confirmation:'HEADING_MEASURED'}]);
    await page.locator('#navField').click();
    await page.waitForTimeout(100);
    assert.equal(commands[1].action,'field');
    state='offline';
    await page.waitForFunction(() => document.querySelector('#imuState').textContent.includes('offline'));
    assert.equal(await page.locator('#rvcDetail').textContent(),'');
    assert.equal(await page.locator('#navField').isDisabled(),true);
    assert.deepEqual(errors,[]);
    fs.writeFileSync(path.join(output,'results.json'),JSON.stringify({passed:true,errors},null,2));
    console.log('PASS: RVC desktop/mobile, no overflow, stale/offline clearing, geometry locked, zero runtime errors');
  } finally { await browser.close(); }
})().catch(error => {console.error(error);process.exitCode=1;});
