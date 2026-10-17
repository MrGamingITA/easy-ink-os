/* Calamaio OS · versione da browser: adatta la tavola alla larghezza della finestra */
(function () {
  'use strict';
  function fit() {
    var wrap = document.querySelector('.board-wrap'), board = document.querySelector('.board');
    if (!wrap || !board) return;
    board.style.transform = '';
    var w = board.offsetWidth, h = board.offsetHeight;
    if (!w || !h) return;
    var avail = document.documentElement.clientWidth - 32;
    var s = Math.min(1, avail / w);
    board.style.transform = s < 1 ? 'scale(' + s + ')' : '';
    wrap.style.width = Math.round(w * s) + 'px';
    wrap.style.height = Math.round(h * s) + 'px';
  }
  window.addEventListener('resize', fit);
  window.addEventListener('load', fit);
  document.addEventListener('DOMContentLoaded', fit);
  setTimeout(fit, 120);
  window.CalamaioFit = fit;
})();
