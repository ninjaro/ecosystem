'use strict';
document.querySelectorAll('[data-diagnostic-report]').forEach(report => {
  const filters = report.querySelectorAll('[data-diagnostic-filter]');
  const groups = report.querySelectorAll('[data-diagnostic-group]');
  const total = report.querySelectorAll('[data-diagnostic-finding]').length;
  const update = () => {
    let shown = 0;
    groups.forEach(group => {
      group.hidden = [...filters].some(filter =>
        filter.value && group.dataset[filter.dataset.diagnosticFilter] !== filter.value);
      if (!group.hidden) shown += group.querySelectorAll('[data-diagnostic-finding]').length;
    });
    report.querySelector('[data-diagnostic-count]').textContent = `${shown}/${total} findings displayed`;
  };
  filters.forEach(filter => filter.addEventListener('change', update));
  report.querySelector('.diagnostic-filters').hidden = false;
  update();
});
