/* Browser integration checks against the explicitly simulated dashboard only. */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {chromium} = require(process.env.PLAYWRIGHT_MODULE || 'playwright');

(async () => {
  const base = process.env.QA_BASE_URL || 'http://127.0.0.1:8876';
  assert(['127.0.0.1', 'localhost'].includes(new URL(base).hostname), 'QA requires a local simulator');
  const status = await (await fetch(base + '/api/operations')).json();
  assert.equal(status.mode, 'simulated', 'Never run browser QA against a real robot');
  const output = path.resolve(process.env.QA_OUTPUT_DIR || 'browser-qa-output');
  fs.mkdirSync(output, {recursive: true});
  const browser = await chromium.launch({channel: process.env.BROWSER_CHANNEL || 'msedge',
    headless: true, args: ['--disable-gpu']});
  const context = await browser.newContext({viewport: {width: 1440, height: 1100}});
  const page = await context.newPage();
  const errors = [], posts = [];
  page.on('pageerror', error => errors.push(error.message));
  page.on('request', request => {
    if (request.method() === 'POST') posts.push(new URL(request.url()).pathname);
  });
  const noOverflow = async () => {
    assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1), true);
  };
  try {
    await page.goto(base);
    await page.waitForFunction(() => document.querySelector('#modeBadge').textContent.includes('SIMULATED'));
    await page.waitForFunction(() => document.querySelector('#rateState').textContent.includes('Fresh'));
    await page.waitForFunction(() => window.UI.live.observed.samples.length >= 8);
    await noOverflow();
    await page.screenshot({path: path.join(output, '01-overview-desktop.png'), fullPage: true});
    await page.getByRole('button', {name: 'Geometry', exact: true}).click();
    for (const [id, value] of Object.entries({wheel_diameter_m: '.1', wheelbase_m: '.4', track_width_m: '.2'})) {
      await page.locator('#' + id).fill(value);
    }
    await Promise.all([page.waitForResponse(response => response.url().endsWith('/api/geometry') && response.ok()),
      page.getByRole('button', {name: 'Save measured geometry'}).click()]);
    await page.screenshot({path: path.join(output, '02-geometry.png'), fullPage: true});
    await page.getByRole('button', {name: 'Recordings', exact: true}).click();
    await page.locator('#captureLabel').fill('SIMULATION browser verification');
    await page.locator('#captureSurface').fill('Synthetic signals');
    await Promise.all([page.waitForResponse(response => response.url().endsWith('/api/recordings/start') && response.ok()),
      page.locator('#captureStart').click()]);
    await page.waitForFunction(() => document.querySelector('#captureState').textContent.includes('active'));
    await page.waitForTimeout(1800); // Capture several independently polled telemetry samples.
    await Promise.all([page.waitForResponse(response => response.url().endsWith('/api/recordings/stop') && response.ok()),
      page.locator('#captureStop').click()]);
    await page.getByRole('button', {name: 'Inspect replay'}).first().waitFor();
    await page.getByRole('button', {name: 'Inspect replay'}).first().click();
    await page.waitForFunction(() => document.querySelector('#modeBadge').textContent.includes('REPLAY'));
    await page.locator('#replayScrub').focus();
    await page.locator('#replayScrub').press('End');
    await page.waitForTimeout(700);
    assert.equal(await page.locator('#captureStart').isDisabled(), true);
    assert.equal(await page.locator('#stop').isDisabled(), false);
    await page.screenshot({path: path.join(output, '03-recording-replay.png'), fullPage: true});
    await page.locator('#replayExit').click();
    await page.waitForFunction(() => document.querySelector('#modeBadge').textContent.includes('SIMULATED'));
    await page.getByRole('button', {name: 'Evidence', exact: true}).click();
    await page.waitForFunction(() => document.querySelector('#evidenceList').textContent.includes('aborted'));
    const options = await page.locator('#compareRight option').evaluateAll(nodes => nodes.map(node => node.value));
    assert(options.length >= 3, 'The actual saved bench reports should load');
    await page.locator('#compareLeft').selectOption(options[0]);
    await page.locator('#compareRight').selectOption(options[1]);
    await page.getByRole('button', {name: 'Compare runs'}).click();
    await page.waitForFunction(() => document.querySelector('#comparison').textContent.includes('Run comparison'));
    await noOverflow();
    await page.screenshot({path: path.join(output, '04-evidence.png'), fullPage: true});
    await page.setViewportSize({width: 390, height: 844});
    for (const name of ['Overview', 'Recordings', 'Evidence', 'Geometry']) {
      await page.getByRole('button', {name, exact: true}).click();
      assert.equal(await page.locator('nav [aria-current="page"]').textContent(), name);
      await noOverflow();
      await page.screenshot({path: path.join(output, 'mobile-' + name.toLowerCase() + '.png'), fullPage: true});
    }
    await page.goto(base + '/tuning');
    await page.getByRole('heading', {name: 'PWM tuning console'}).waitFor();
    const allowed = new Set(['/api/geometry', '/api/recordings/start', '/api/recordings/stop',
      '/api/replay', '/api/evidence/compare']);
    assert(posts.every(url => allowed.has(url)), 'Unexpected action route: ' + posts.join(', '));
    assert.deepEqual(errors, [], 'Browser runtime errors');
    fs.writeFileSync(path.join(output, 'results.json'), JSON.stringify({passed: true, posts, errors}, null, 2));
    console.log('PASS: desktop/mobile, geometry, capture/replay, evidence comparison, retained tuning, zero browser errors');
  } catch (error) {
    await page.screenshot({path: path.join(output, 'failure.png'), fullPage: true}).catch(() => {});
    fs.writeFileSync(path.join(output, 'results.json'), JSON.stringify({passed: false, posts, errors,
      error: String(error)}, null, 2));
    throw error;
  } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
