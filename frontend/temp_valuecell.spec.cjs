const { test } = require('playwright/test');

test('valuecell tcom debug', async ({ page, browserName }) => {
  const logs = [];
  page.on('console', msg => logs.push(`console:${msg.type()}:${msg.text()}`));
  page.on('pageerror', err => logs.push(`pageerror:${err.stack || err.message}`));
  page.on('requestfailed', req => logs.push(`requestfailed:${req.url()}::${req.failure()?.errorText}`));

  await page.goto('http://127.0.0.1:5173/valuecell', { waitUntil: 'domcontentloaded', timeout: 60000 });
  await page.waitForTimeout(3000);
  await page.screenshot({ path: 'D:/AI_Investment/workbook/image/automation/pw_before.png', fullPage: true });
  await page.locator('input').first().fill('TCOM');
  await page.locator('button').first().click();
  await page.waitForTimeout(35000);
  await page.screenshot({ path: 'D:/AI_Investment/workbook/image/automation/pw_after.png', fullPage: true });

  console.log(logs.join('\n'));
});
