/* Read-only evidence: missing and aborted measurements keep their labels. */
(() => {
  const UI = window.UI, wheels = ['FL', 'FR', 'RL', 'RR'], directions = ['forward', 'reverse'];
  let bound = false, loadGeneration = 0, compareGeneration = 0, comparing = false;
  const value = (item, missing = 'unrecorded') => item == null ? missing
    : typeof item === 'object' ? JSON.stringify(item) : String(item);
  const source = report => report.simulated === true ? ' · SIMULATED'
    : report.simulated === false ? '' : ' · SOURCE UNRECORDED';
  function table(headers, rows) {
    const result = UI.node('table'), head = UI.node('thead'), heading = UI.node('tr'), body = UI.node('tbody');
    headers.forEach(label => heading.append(UI.node('th', label))); head.append(heading);
    for (const cells of rows) {
      const row = UI.node('tr'); cells.forEach(cell => row.append(UI.node('td', value(cell)))); body.append(row);
    }
    result.append(head, body); return result;
  }
  function startup(report) {
    const result = table(['Start PWM', ...wheels], []), body = result.querySelector('tbody');
    for (const direction of directions) {
      const row = UI.node('tr'); row.append(UI.node('td', direction));
      for (const wheel of wheels) {
        const sample = report.startup?.[direction]?.[wheel];
        const measured = sample?.status === 'measured' && Number.isFinite(sample.minimum_start_pwm);
        const cell = UI.node('td', measured ? String(sample.minimum_start_pwm) : '—');
        cell.title = measured ? `${value(sample.confirmation_starts)} confirmed starts` : 'Not measured';
        row.append(cell);
      }
      body.append(row);
    }
    return result;
  }
  function comparisonButton() {
    UI.el('compareForm').querySelector('button').disabled = comparing || UI.reports.length < 2
      || !UI.el('compareLeft').value || UI.el('compareLeft').value === UI.el('compareRight').value;
  }
  function options(reports) {
    const ids = reports.map(report => report.id), left = UI.el('compareLeft'), right = UI.el('compareRight');
    const oldLeft = left.value, oldRight = right.value;
    for (const select of [left, right]) {
      select.replaceChildren();
      for (const id of ids) { const option = UI.node('option', id); option.value = id; select.append(option); }
    }
    left.value = ids.includes(oldLeft) ? oldLeft : ids[0] || '';
    right.value = ids.includes(oldRight) ? oldRight : ids.find(id => id !== left.value) || ids[0] || '';
    comparisonButton();
  }
  function card(report) {
    const result = UI.node('article', '', 'report-card'), heading = UI.node('div', '', 'row');
    heading.append(UI.node('h3', `${value(report.id)} · ${value(report.kind)}`),
      UI.node('span', value(report.status) + source(report), report.status === 'aborted' ? 'badge warn' : 'badge'));
    result.append(heading);
    if (report.error) result.append(UI.node('p', String(report.error), 'error'));
    const restored = report.restored === true ? 'yes' : report.restored === false ? 'no' : 'unrecorded';
    result.append(table(['Recorded fact', 'Value'], [
      ['Original settings restored', restored],
      ['Completed / attempted diagnostic trials', `${value(report.completed_trial_count, '—')} / ${value(report.trial_count, '—')}`],
      ['Configured PWM ceiling', value(report.maximum_pwm, '—')],
      ['Requested trial peak PWM', value(report.maximum_trial_pwm, '—')]]));
    result.append(startup(report));
    result.append(table(['Directional matching', 'Status', 'PWM'], directions.map(direction =>
      [direction, report.directional_matching?.[direction]?.status, report.directional_matching?.[direction]?.pwm])));
    if (report.recommended_shared_pwm != null) result.append(
      UI.node('p', 'Recorded recommendation (read-only)'), UI.node('pre', value(report.recommended_shared_pwm)));
    const conditions = {...report.conditions};
    for (const key of ['battery_voltage', 'surface', 'load']) if (!(key in conditions)) conditions[key] = null;
    result.append(table(['Condition', 'Recorded value'], Object.entries(conditions).map(([key, item]) =>
      [key.replaceAll('_', ' '), item])));
    return result;
  }
  UI.loadEvidence = async function() {
    const version = ++loadGeneration, list = UI.el('evidenceList');
    try {
      const data = await UI.api('/api/evidence');
      if (version !== loadGeneration) return;
      UI.reports = Array.isArray(data.reports) ? data.reports : [];
      compareGeneration++; list.replaceChildren();
      UI.reports.forEach(report => list.append(card(report)));
      if (!UI.reports.length) list.append(UI.node('p', 'No bench evidence reports found.', 'empty'));
      for (const error of data.errors || []) list.append(UI.node('p',
        `${error.id || 'Report'}: ${value(error.error, 'Unable to read report')}`, 'error'));
      options(UI.reports);
    } catch (error) {
      if (version !== loadGeneration) return;
      UI.reports = []; options([]);
      list.replaceChildren(UI.node('p', 'Evidence unavailable: ' + (error.message || error), 'error'));
      UI.notify(error.message || String(error), true);
    }
  };
  function renderComparison(data) {
    const result = UI.el('comparison'); result.replaceChildren(UI.node('h2', 'Run comparison'),
      UI.node('p', 'Delta = right minus left. Missing conditions prevent a controlled comparison.'));
    result.append(UI.node('p', `Left: ${value(data.left?.id)} · ${value(data.left?.status)}${source(data.left || {})}`
      + ` | Right: ${value(data.right?.id)} · ${value(data.right?.status)}${source(data.right || {})}`));
    result.append(table(['Condition', 'Left', 'Right', 'Status'], Object.entries(data.conditions || {}).map(
      ([field, condition]) => [field.replaceAll('_', ' '), condition.left, condition.right, condition.status])));
    result.append(table(['Start PWM delta', ...wheels], directions.map(direction =>
      [direction, ...wheels.map(wheel => value(data.threshold_deltas?.[direction]?.[wheel], '—'))])));
  }
  UI.bindEvidence = function() {
    if (bound) return; bound = true;
    for (const id of ['compareLeft', 'compareRight']) UI.el(id).addEventListener('change', () => {
      compareGeneration++; comparisonButton();
    });
    UI.el('compareForm').addEventListener('submit', UI.run(async () => {
      const left = UI.el('compareLeft').value, right = UI.el('compareRight').value;
      if (comparing) return;
      if (!left || !right || left === right) throw new Error('Choose two different runs to compare.');
      const version = ++compareGeneration; comparing = true; comparisonButton();
      try {
        const data = await UI.api('/api/evidence/compare', {left, right});
        if (version === compareGeneration && left === UI.el('compareLeft').value
            && right === UI.el('compareRight').value) renderComparison(data);
      } finally { comparing = false; comparisonButton(); }
    }));
    comparisonButton();
  };
})();
