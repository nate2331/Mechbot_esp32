/* Shared application state. Robot motion stays in the existing tuning workflow. */
(() => {
  const UI = {
    live: null, replay: null, playing: false, view: 'overview', reports: [], pollBusy: false,
    connectionErrorMessage: null,
    el(id) { return document.getElementById(id); },
    text(id, value) { const element = this.el(id); if (element) element.textContent = value; },
    fmt(value, decimals = 1) { return Number.isFinite(value) ? value.toFixed(decimals) : '—'; },
    node(tag, text = '', className = '') {
      const element = document.createElement(tag);
      element.textContent = text;
      element.className = className;
      return element;
    },
    notify(message, error = false) {
      this.text('notice', message);
      this.el('notice').className = error ? 'error' : '';
    },
    async api(path, data) {
      const controller = new AbortController();
      const timeout = setTimeout(() => controller.abort(), 8000);
      try {
        const options = {signal: controller.signal};
        if (data !== undefined) {
          options.method = 'POST';
          options.headers = {'Content-Type': 'application/json'};
          options.body = JSON.stringify(data);
        }
        const response = await fetch(path.startsWith('/api/') ? path : '/api' + path, options);
        const result = await response.json();
        if (!response.ok) throw new Error(result.error || 'Request failed');
        return result;
      } finally { clearTimeout(timeout); }
    },
    run(action) {
      return async event => {
        event?.preventDefault();
        try { await action(event); }
        catch (error) { UI.notify(error.message || String(error), true); }
      };
    },
    showView(name) {
      if (this.replay && ['overview', 'geometry'].includes(name)) {
        this.notify('Exit replay to return to live observations and geometry.');
        name = 'recordings';
      }
      this.view = name;
      document.querySelectorAll('.view').forEach(view => { view.hidden = view.id !== 'view-' + name; });
      document.querySelectorAll('nav [data-view]').forEach(button => {
        if (button.dataset.view === name) button.setAttribute('aria-current', 'page');
        else button.removeAttribute('aria-current');
      });
      const load = name === 'recordings' ? this.loadRecordings : name === 'evidence' ? this.loadEvidence : null;
      if (load) Promise.resolve(load()).catch(error => this.notify(error.message, true));
      if (!this.replay && this.live) this.renderLive(this.live.observed, this.live.mode, this.live.bridge);
    },
    async refresh() {
      if (this.pollBusy) return;
      this.pollBusy = true;
      try {
        const data = await this.api('/api/operations');
        if (this.connectionErrorMessage && this.el('notice').textContent === this.connectionErrorMessage) {
          this.notify('Connection restored.');
        }
        this.connectionErrorMessage = null;
        this.live = data;
        this.updateEncoderCheck?.(data);
        if (!this.replay) this.renderLive(data.observed, data.mode, data.bridge);
        const capture = data.capture || {};
        this.text('captureState', [capture.state, capture.written + ' saved', capture.dropped + ' dropped',
          capture.error].filter(value => value != null).join(' · '));
        const active = ['active', 'stopping'].includes(capture.state);
        this.el('captureStart').disabled = active || !!this.replay;
        this.el('captureStop').disabled = !active;
        if (active || this.replay) {
          this.el('geometryForm').querySelector('button').disabled = true;
          this.el('resetPose').disabled = true;
        }
        if (data.config_error) this.notify('Geometry configuration: ' + data.config_error, true);
        if (data.bridge?.operations_error) {
          const error = data.bridge.operations_error;
          this.notify('Observation fault (' + error.method + '): ' + error.error, true);
        }
      } catch (error) {
        this.updateEncoderCheck?.(null);
        if (!this.replay) this.renderLive(null, 'offline', {});
        this.connectionErrorMessage = 'Connection unavailable: ' + (error.message || error);
        this.notify(this.connectionErrorMessage, true);
      } finally { this.pollBusy = false; }
    }
  };
  window.UI = UI;
  document.addEventListener('DOMContentLoaded', () => {
    document.querySelectorAll('nav [data-view]').forEach(button =>
      button.addEventListener('click', UI.run(() => UI.showView(button.dataset.view))));
    UI.el('stop').addEventListener('click', UI.run(async () => {
      await UI.api('/api/stop', {});
      UI.notify('Stop command sent.');
    }));
    UI.el('geometryForm').addEventListener('submit', UI.run(async () => {
      if (UI.replay) throw new Error('Exit replay before changing geometry.');
      const values = {};
      ['wheel_diameter_m', 'wheelbase_m', 'track_width_m'].forEach(key => {
        if (!UI.el(key).value.trim()) throw new Error('Enter all three measured dimensions.');
        values[key] = Number(UI.el(key).value);
      });
      await UI.api('/api/geometry', values);
      UI.notify('Measured geometry saved. Pose origin reset.');
      await UI.refresh();
    }));
    UI.el('resetPose').addEventListener('click', UI.run(async () => {
      if (UI.replay) throw new Error('Exit replay before resetting the live pose.');
      await UI.api('/api/odometry/reset', {});
      UI.notify('Pose origin reset.');
      await UI.refresh();
    }));
    UI.bindSessions();
    UI.bindEvidence();
    UI.showView('overview');
    const poll = async () => { await UI.refresh(); setTimeout(poll, 500); };
    poll();
  });
})();
