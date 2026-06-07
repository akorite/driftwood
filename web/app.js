/* =========================================================================
   DriftWood Web UI - Application Logic
   Uses chess.js for game state and chessboard.js (oakmac) for the board.
   Highlights (last-move, check, selected, legal destinations) are drawn
   as DOM overlays on top of the chessboard.js squares.
   ========================================================================= */

(function() {
'use strict';

// --- Configuration ---
const PIECE_THEME = '/vendor/img/chesspieces/wikipedia/{piece}.png';
const INITIAL_TIME = 600; // overwritten by #time-select on new game

// --- State ---
let board = null;          // chessboard.js widget
let chess = null;          // chess.js instance
let humanColor = 'w';      // 'w' or 'b'
let gameId = null;
let isGameOver = false;
let isEngineThinking = false;
let gameHistory = [];      // Array of { fen, uci, san, moveNumber, captured }
let pendingUci = null;     // user's move awaiting server response

// Clock state (in seconds)
let timeWhite = INITIAL_TIME;
let timeBlack = INITIAL_TIME;
let clockInterval = null;

// Map from square name ("e2") -> DOM element (populated on init)
let squaresByName = new Map();

// DOM refs
const boardEl = document.getElementById('chessboard');
const moveListEl = document.getElementById('move-list');
const gameStatusEl = document.getElementById('game-status');
const evalDisplayEl = document.getElementById('eval-display');
const depthDisplayEl = document.getElementById('depth-display');
const modalOverlay = document.getElementById('modal-overlay');
const modalTitle = document.getElementById('modal-title');
const modalMessage = document.getElementById('modal-message');
const moveIndicator = document.getElementById('move-indicator');
const moveIndicatorText = document.getElementById('move-indicator-text');
const boardMessageEl = document.getElementById('board-message');

const opponentClockEl = document.getElementById('opponent-clock');
const playerClockEl = document.getElementById('player-clock');
const opponentCapturedEl = document.getElementById('opponent-captured');
const playerCapturedEl = document.getElementById('player-captured');

const btnNewGame = document.getElementById('btn-new-game');
const btnResign = document.getElementById('btn-resign');
const btnDraw = document.getElementById('btn-offer-draw');
const btnFlip = document.getElementById('btn-flip');
const btnModalNewGame = document.getElementById('btn-modal-new-game');
const sideSelect = document.getElementById('side-select');
const timeSelect = document.getElementById('time-select');

// --- Helpers ---

function colorName(c) {
  return c === 'w' ? 'White' : 'Black';
}

function uciToMove(uci) {
  const m = { from: uci.substring(0, 2), to: uci.substring(2, 4) };
  if (uci[4]) m.promotion = uci[4];
  return m;
}

function getLastMoveSquares() {
  if (gameHistory.length === 0) return [];
  const last = gameHistory[gameHistory.length - 1];
  return [last.uci.substring(0, 2), last.uci.substring(2, 4)];
}

function getLegalDests() {
  const dests = new Map();
  if (!chess) return dests;
  try {
    const moves = chess.moves({ verbose: true });
    for (const m of moves) {
      if (!dests.has(m.from)) dests.set(m.from, []);
      dests.get(m.from).push(m.to);
    }
  } catch (e) {
    console.warn('chess.js moves() failed:', e);
  }
  return dests;
}

function pieceClass(pieceChar, color) {
  const colorMap = { 'w': 'white', 'b': 'black' };
  const pieceMap = { 'p': 'pawn', 'n': 'knight', 'b': 'bishop', 'r': 'rook', 'q': 'queen', 'k': 'king' };
  return colorMap[color] + ' ' + pieceMap[pieceChar];
}

function pieceGlyph(pieceChar, color) {
  // Unicode chess pieces (filled, inverting color for visibility)
  const white = { 'p':'\u2659', 'n':'\u2658', 'b':'\u2657', 'r':'\u2656', 'q':'\u2655', 'k':'\u2654' };
  const black = { 'p':'\u265F', 'n':'\u265E', 'b':'\u265D', 'r':'\u265C', 'q':'\u265B', 'k':'\u265A' };
  return color === 'w' ? white[pieceChar] : black[pieceChar];
}

// --- Clock Management ---

function formatClock(seconds) {
  if (seconds < 0) seconds = 0;
  const m = Math.floor(seconds / 60);
  const s = Math.floor(seconds % 60);
  return `${m}:${s.toString().padStart(2, '0')}`;
}

function updateClockDisplay() {
  opponentClockEl.textContent = formatClock(humanColor === 'w' ? timeBlack : timeWhite);
  playerClockEl.textContent = formatClock(humanColor === 'w' ? timeWhite : timeBlack);
  const oppT = humanColor === 'w' ? timeBlack : timeWhite;
  const plyT = humanColor === 'w' ? timeWhite : timeBlack;
  opponentClockEl.classList.toggle('low-time', oppT <= 30);
  playerClockEl.classList.toggle('low-time', plyT <= 30);
}

function startClock() {
  if (clockInterval) clearInterval(clockInterval);
  clockInterval = setInterval(() => {
    if (isGameOver) return;
    // The clock is always for the side whose turn it is.
    // During the engine's think phase we revert chess.js to the pre-move
    // state before the fetch, so chess.turn() still says "human". In that
    // window we need to tick the engine's clock instead — use the
    // isEngineThinking flag to flip the side.
    const sideOnMove = isEngineThinking
      ? (humanColor === 'w' ? 'b' : 'w')
      : chess.turn();
    if (sideOnMove === 'w') timeWhite = Math.max(0, timeWhite - 1);
    else timeBlack = Math.max(0, timeBlack - 1);
    updateClockDisplay();
    if (timeWhite <= 0 || timeBlack <= 0) {
      endGameByTimeout();
    }
  }, 1000);
}

function stopClock() {
  if (clockInterval) { clearInterval(clockInterval); clockInterval = null; }
}

function resetClocks(initialSeconds) {
  stopClock();
  timeWhite = initialSeconds;
  timeBlack = initialSeconds;
  updateClockDisplay();
  if (!isGameOver) startClock();
}

function endGameByTimeout() {
  if (isGameOver) return;
  isGameOver = true;
  stopClock();
  const loser = timeWhite <= 0 ? 'White' : 'Black';
  const winner = loser === 'White' ? 'Black' : 'White';
  showModal('Time Out!', `${loser} ran out of time. ${winner} wins!`);
  updateControls();
  opponentClockEl.classList.add('ended');
  playerClockEl.classList.add('ended');
  if (board) board.position(chess.fen(), false);
}

// --- Board Overlays (highlights) ---

function rebuildSquaresMap() {
  squaresByName.clear();
  // chessboard.js adds data-square to each square element
  const squares = boardEl.querySelectorAll('[data-square]');
  for (const sq of squares) {
    squaresByName.set(sq.getAttribute('data-square'), sq);
  }
}

function clearHighlights() {
  for (const sq of squaresByName.values()) {
    const hl = sq.querySelector('.square-highlight');
    if (hl) hl.remove();
  }
}

function addHighlight(squareName, type) {
  const sq = squaresByName.get(squareName);
  if (!sq) return;
  const hl = document.createElement('div');
  hl.className = 'square-highlight ' + type;
  // Insert as first child so it sits beneath the piece image
  if (sq.firstChild) sq.insertBefore(hl, sq.firstChild);
  else sq.appendChild(hl);
}

function applyHighlights({ lastMove = [], check = null, selected = null, legal = null } = {}) {
  clearHighlights();
  for (const sq of lastMove) addHighlight(sq, 'last-move');
  if (check) addHighlight(check, 'check');
  if (selected) addHighlight(selected, 'selected');
  if (legal) {
    for (const dest of legal) {
      const isCapture = chess.get(dest) !== null;
      addHighlight(dest, isCapture ? 'legal-ring' : 'legal-dot');
    }
  }
}

function updateBoardState({ animate = true, selected = null, legal = null } = {}) {
  if (!board || !chess) return;
  const fen = chess.fen();
  // chessboard.js uses FEN with full or partial info
  // We pass the full FEN (chess.js returns it with all fields)
  // Disable animation if user is mid-drag
  board.position(fen, animate);
  // Rebuild squares map (chessboard.js keeps them but we want to be safe)
  if (squaresByName.size === 0) rebuildSquaresMap();
  // Re-apply overlays
  const lastMove = getLastMoveSquares();
  let checkSq = null;
  if (chess.in_check()) {
    // Find king of side to move
    const turn = chess.turn();
    const board2 = chess.board();
    for (let r = 0; r < 8; r++) {
      for (let c = 0; c < 8; c++) {
        const p = board2[r][c];
        if (p && p.type === 'k' && p.color === turn) {
          const col = 'abcdefgh'[c];
          const row = 8 - r;
          checkSq = col + row;
        }
      }
    }
  }
  applyHighlights({ lastMove, check: checkSq, selected, legal });
}

// --- Captured pieces ---

function updateCaptured() {
  // Recompute from history: white captures means black pieces removed
  let wCaptured = [];
  let bCaptured = [];
  for (const entry of gameHistory) {
    if (entry.captured) {
      // entry.captured is the captured piece char; the capturing side
      // is the side who just moved. The captured piece is the OTHER side.
      // Move number 1 (white) captures a black piece => bCaptured
      // Move number 2 (black) captures a white piece => wCaptured
      if (entry.moveNumber % 2 === 1) bCaptured.push(entry.captured);
      else wCaptured.push(entry.captured);
    }
  }
  // Sort by piece value, descending (Q, R, B, N, P)
  const order = { q: 5, r: 4, b: 3, n: 2, p: 1 };
  const sortFn = (a, b) => (order[b.charAt(1)] || 0) - (order[a.charAt(1)] || 0);
  wCaptured.sort(sortFn);
  bCaptured.sort(sortFn);
  // Top side shows what opponent lost from player's perspective:
  //   If human=white: opponent is black, so opponent's captures = what black has taken = wCaptured (white pieces lost)
  //                   player captures = bCaptured (black pieces lost)
  //   If human=black: symmetric
  const opponentLost = humanColor === 'w' ? wCaptured : bCaptured;  // pieces opponent has captured FROM player
  const playerLost = humanColor === 'w' ? bCaptured : wCaptured;    // pieces player has captured FROM opponent
  // opponent side displays: pieces opponent has captured (from player's view: "their captures")
  // player side displays: pieces player has captured
  opponentCapturedEl.innerHTML = opponentLost.map(c =>
    `<span class="captured-glyph ${c.color}">${pieceGlyph(c.char, c.color)}</span>`
  ).join('');
  playerCapturedEl.innerHTML = playerLost.map(c =>
    `<span class="captured-glyph ${c.color}">${pieceGlyph(c.char, c.color)}</span>`
  ).join('');
}

// --- Move History ---

function updateMoveHistory() {
  moveListEl.innerHTML = '';
  for (let i = 0; i < gameHistory.length; i += 2) {
    const moveNum = Math.floor(i / 2) + 1;
    const row = document.createElement('div');
    row.className = 'move-row';
    const numSpan = document.createElement('span');
    numSpan.className = 'move-number';
    numSpan.textContent = moveNum + '.';
    row.appendChild(numSpan);
    if (i < gameHistory.length) {
      const wMove = document.createElement('span');
      wMove.className = 'move-uci' + (i === gameHistory.length - 1 ? ' current-move' : '');
      wMove.textContent = gameHistory[i].san;
      wMove.dataset.index = i;
      wMove.addEventListener('click', () => jumpToMove(i));
      row.appendChild(wMove);
    }
    if (i + 1 < gameHistory.length) {
      const bMove = document.createElement('span');
      bMove.className = 'move-uci' + (i + 1 === gameHistory.length - 1 ? ' current-move' : '');
      bMove.textContent = gameHistory[i + 1].san;
      bMove.dataset.index = i + 1;
      bMove.addEventListener('click', () => jumpToMove(i + 1));
      row.appendChild(bMove);
    }
    moveListEl.appendChild(row);
  }
  moveListEl.scrollTop = moveListEl.scrollHeight;
}

function jumpToMove(index) {
  if (index < 0 || index >= gameHistory.length) return;
  const entry = gameHistory[index];
  // Replay the game from start up to index
  const c = new Chess();
  for (let i = 0; i <= index; i++) {
    c.move(uciToMove(gameHistory[i].uci));
  }
  chess = c;
  updateBoardState({ animate: false });
  updateClockDisplay();
  // Highlight the current move in the list
  moveListEl.querySelectorAll('.move-uci').forEach(el => el.classList.remove('current-move'));
  moveListEl.querySelectorAll(`.move-uci[data-index="${index}"]`).forEach(el => el.classList.add('current-move'));
}

// --- Game Flow ---

// Called when the user drops a piece in chessboard.js
function onUserMove(source, target, piece, newPos, oldPos, orientation) {
  if (isGameOver || isEngineThinking) return;
  if (!target) return;            // dropped off board
  if (source === target) return;  // no-op

  // chessboard.js already validated the piece color; we just need to validate
  // it's the human's turn and the move is legal under chess.js
  if (chess.turn() !== humanColor) {
    // wrong color: don't let the move stick
    return 'snapback';
  }

  // Detect promotion: pawn moving to last rank
  const isPromotion = (piece === 'wP' && target[1] === '8') ||
                      (piece === 'bP' && target[1] === '1');
  const moveStr = source + target + (isPromotion ? 'q' : '');

  // Validate locally with chess.js
  const validationMove = chess.move(uciToMove(moveStr));
  if (!validationMove) {
    return 'snapback';
  }
  // Undo: we apply the real state from the server response
  chess.undo();
  // Revert the board to the pre-move state while we wait
  board.position(chess.fen(), true);

  pendingUci = moveStr;
  isEngineThinking = true;
  moveIndicator.classList.remove('hidden');
  moveIndicatorText.textContent = 'Engine thinking...';
  updateBoardState({ animate: true, selected: null, legal: new Map() });

  const fenBefore = chess.fen();

  // Send clocks (in ms) so the server can budget engine time proportionally.
  // It is the side-to-move's clock that matters; we send both for completeness.
  fetch('/api/move', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      fen: fenBefore,
      move: moveStr,
      wtime: Math.floor(timeWhite * 1000),
      btime: Math.floor(timeBlack * 1000)
    })
  })
    .then(r => r.json())
    .then(data => {
      if (!data.legal) {
        isEngineThinking = false;
        pendingUci = null;
        moveIndicator.classList.add('hidden');
        updateBoardState({ animate: true });
        return;
      }
      // Apply user's move into the chess.js state
      const userMove = chess.move(uciToMove(data.user_move));
      const userSan = userMove ? userMove.san : data.user_move;
      const userCaptured = userMove && userMove.captured
        ? { char: userMove.captured, color: userMove.color === 'w' ? 'b' : 'w' }
        : null;
      gameHistory.push({
        fen: fenBefore,
        uci: data.user_move,
        san: userSan,
        moveNumber: gameHistory.length + 1,
        captured: userCaptured
      });

      // Apply engine's response (if any)
      if (data.engine_move && data.engine_move !== '') {
        const engMove = chess.move(uciToMove(data.engine_move));
        const engFen = chess.fen();
        const engSan = engMove ? engMove.san : data.engine_move;
        const engCaptured = engMove && engMove.captured
          ? { char: engMove.captured, color: engMove.color === 'w' ? 'b' : 'w' }
          : null;
        gameHistory.push({
          fen: engFen,
          uci: data.engine_move,
          san: engSan,
          moveNumber: gameHistory.length + 1,
          captured: engCaptured
        });
      }

      isEngineThinking = false;
      pendingUci = null;
      moveIndicator.classList.add('hidden');

      // If we navigated into the past, the chess instance has the latest state
      updateBoardState({ animate: true });
      updateMoveHistory();
      updateCaptured();
      updateStatus(data);
      updateControls();

      if (data.is_game_over === 'true' || data.is_game_over === true) {
        endGame(data);
      } else {
        // Refresh eval for the new position
        requestEval();
      }
    })
    .catch(err => {
      console.error('API error:', err);
      isEngineThinking = false;
      pendingUci = null;
      moveIndicator.classList.add('hidden');
      moveIndicatorText.textContent = 'Connection error';
      moveIndicator.classList.remove('hidden');
      setTimeout(() => moveIndicator.classList.add('hidden'), 3000);
      updateBoardState({ animate: true });
    });
}

// Show legal destinations when the user picks up a piece
function onDragStart(source, piece, position, orientation) {
  if (isGameOver || isEngineThinking) return false;
  if (chess.turn() !== humanColor) return false;
  // piece is e.g. "wP"; only allow own color
  const color = piece.charAt(0);
  if (color !== humanColor) return false;
  // Show destinations
  const dests = getLegalDests();
  const pieceDests = dests.get(source) || [];
  applyHighlights({
    lastMove: getLastMoveSquares(),
    selected: source,
    legal: pieceDests
  });
  return true;
}

function onDragMove(newSquare, oldSquare, source, piece, position, orientation) {
  // No-op for now; could update legal-dest highlight on hover
}

function onSnapEnd() {
  // After a successful drop animation, redraw highlights
  // (chess.js state will be updated separately by server response)
  if (pendingUci) {
    // Show only last-move and check highlights while waiting
    applyHighlights({
      lastMove: getLastMoveSquares(),
      check: findKingInCheck()
    });
  } else {
    // It's the engine's move that just animated; nothing else to do
  }
}

function findKingInCheck() {
  if (!chess.in_check()) return null;
  const turn = chess.turn();
  const b = chess.board();
  for (let r = 0; r < 8; r++) {
    for (let c = 0; c < 8; c++) {
      const p = b[r][c];
      if (p && p.type === 'k' && p.color === turn) {
        return 'abcdefgh'[c] + (8 - r);
      }
    }
  }
  return null;
}

// --- Status & Game Over ---

function updateStatus(data) {
  let status = `${colorName(humanColor)} to move`;
  if (chess.turn() !== humanColor) status = 'Engine thinking...';
  if (data) {
    if (data.is_check === 'true' || data.is_check === true) status = 'Check!';
    if (data.is_game_over === 'true' || data.is_game_over === true) {
      if (data.result === '1-0') status = 'White wins';
      else if (data.result === '0-1') status = 'Black wins';
      else if (data.result === '1/2-1/2') status = 'Draw';
    }
  }
  gameStatusEl.textContent = status;
}

function updateControls() {
  btnResign.disabled = isGameOver || isEngineThinking;
  btnDraw.disabled = isGameOver || isEngineThinking;
}

function endGame(data) {
  isGameOver = true;
  stopClock();
  opponentClockEl.classList.add('ended');
  playerClockEl.classList.add('ended');
  updateStatus(data);
  updateControls();
  let title = 'Game Over';
  let message = '';
  const r = data.result;
  if (r === '1-0') message = 'White wins!';
  else if (r === '0-1') message = 'Black wins!';
  else if (r === '1/2-1/2') message = 'Draw!';
  if (data.is_checkmate === 'true' || data.is_checkmate === true) {
    const winner = r === '1-0' ? 'White' : 'Black';
    message = `Checkmate! ${winner} wins!`;
  } else if (data.is_stalemate === 'true' || data.is_stalemate === true) {
    message = 'Stalemate!';
  } else if (data.is_insufficient_material === 'true' || data.is_insufficient_material === true) {
    message = 'Draw (insufficient material).';
  } else if (data.is_threefold_repetition === 'true' || data.is_threefold_repetition === true) {
    message = 'Draw (threefold repetition).';
  } else if (data.is_fifty_move_rule === 'true' || data.is_fifty_move_rule === true) {
    message = 'Draw (50-move rule).';
  }
  showModal(title, message);
}

function showModal(title, message) {
  modalTitle.textContent = title;
  modalMessage.textContent = message;
  modalOverlay.classList.remove('hidden');
}
function hideModal() { modalOverlay.classList.add('hidden'); }
function showBoardMessage(msg) {
  if (!msg) { boardMessageEl.classList.add('hidden'); boardMessageEl.textContent = ''; return; }
  boardMessageEl.textContent = msg;
  boardMessageEl.classList.remove('hidden');
}

// --- New Game ---

function startNewGame() {
  hideModal();
  showBoardMessage(null);
  stopClock();
  stopEvalUpdates();

  // Tear down old board
  if (board) {
    try { board.destroy(); } catch (e) { /* ignore */ }
    board = null;
  }
  squaresByName.clear();

  chess = new Chess();
  gameHistory = [];
  pendingUci = null;
  isGameOver = false;
  isEngineThinking = false;

  const colorParam = sideSelect.value;
  if (colorParam === 'random') {
    humanColor = Math.random() < 0.5 ? 'w' : 'b';
  } else {
    humanColor = colorParam === 'black' ? 'b' : 'w';
  }
  const initialSeconds = parseInt(timeSelect.value, 10) || INITIAL_TIME;

  moveListEl.innerHTML = '';
  opponentCapturedEl.innerHTML = '';
  playerCapturedEl.innerHTML = '';
  moveIndicator.classList.add('hidden');
  opponentClockEl.classList.remove('ended');
  playerClockEl.classList.remove('ended');
  evalDisplayEl.textContent = 'Eval: -';
  depthDisplayEl.textContent = 'Depth: -';
  gameStatusEl.textContent = 'Starting...';

  // Create the board
  const orientation = humanColor === 'w' ? 'white' : 'black';
  const cfg = {
    pieceTheme: PIECE_THEME,
    position: 'start',
    orientation: orientation,
    draggable: true,
    showNotation: true,
    appearSpeed: 200,
    moveSpeed: 200,
    snapbackSpeed: 100,
    snapSpeed: 50,
    trashSpeed: 100,
    onDragStart: onDragStart,
    onDragMove: onDragMove,
    onDrop: onUserMove,
    onSnapEnd: onSnapEnd,
    onChange: () => {
      // Re-collect square refs after first render
      if (squaresByName.size === 0) rebuildSquaresMap();
    }
  };
  board = ChessBoard('chessboard', cfg);
  // chessboard.js builds DOM synchronously; collect square refs
  rebuildSquaresMap();
  applyHighlights({ lastMove: [], check: findKingInCheck() });

  // Tell the server to start a new game
  fetch(`/api/new_game?color=${colorParam}&time=${initialSeconds}`)
    .then(r => r.json())
    .then(data => {
      gameId = data.game_id;
      if (data.fen) {
        chess = new Chess(data.fen);
        board.position(data.fen, true);
      }
      if (data.engine_move && data.engine_move !== '') {
        // Engine moved first (we're playing black)
        const engMove = chess.move(uciToMove(data.engine_move));
        const engFen = chess.fen();
        const engSan = engMove ? engMove.san : data.engine_move;
        const engCaptured = engMove && engMove.captured
          ? { char: engMove.captured, color: engMove.color === 'w' ? 'b' : 'w' }
          : null;
        gameHistory.push({
          fen: 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1',
          uci: data.engine_move,
          san: engSan,
          moveNumber: 1,
          captured: engCaptured
        });
        board.position(chess.fen(), true);
        updateMoveHistory();
        updateCaptured();
      }
      updateBoardState({ animate: true });
      resetClocks(initialSeconds);
      updateControls();
      if (data.is_game_over === 'true' || data.is_game_over === true) {
        endGame(data);
      } else {
        updateStatus(data);
        requestEval();
      }
    })
    .catch(err => {
      console.error('Failed to start game:', err);
      showBoardMessage('Could not reach the engine server. Is `driftwood serve` running?');
      gameStatusEl.textContent = 'Connection error';
    });
}

// --- Resign / Draw ---

function resignGame() {
  if (isGameOver || isEngineThinking) return;
  if (!confirm('Resign this game?')) return;
  isGameOver = true;
  stopClock();
  const winner = humanColor === 'w' ? 'Black' : 'White';
  showModal('You resigned', `${winner} wins!`);
  gameStatusEl.textContent = `${winner} wins by resignation`;
  updateControls();
  opponentClockEl.classList.add('ended');
  playerClockEl.classList.add('ended');
}

function offerDraw() {
  if (isGameOver || isEngineThinking) return;
  // For now, treat a draw offer as accepted
  isGameOver = true;
  stopClock();
  showModal('Draw', 'Draw agreed. 1/2 - 1/2');
  gameStatusEl.textContent = 'Draw agreed';
  updateControls();
  opponentClockEl.classList.add('ended');
  playerClockEl.classList.add('ended');
}

// --- Eval display ---

let evalInterval = null;
function startEvalUpdates() {
  stopEvalUpdates();
  evalInterval = setInterval(() => {
    if (isGameOver || isEngineThinking) return;
    requestEval();
  }, 4000);
}
function stopEvalUpdates() {
  if (evalInterval) { clearInterval(evalInterval); evalInterval = null; }
}
function requestEval() {
  if (!chess) return;
  const fen = chess.fen();
  fetch(`/api/eval?fen=${encodeURIComponent(fen)}&depth=8`)
    .then(r => r.json())
    .then(data => {
      if (typeof data.score_cp !== 'number') return;
      const cp = data.score_cp / 100;
      const sign = cp > 0 ? '+' : '';
      const display = cp > 99 ? `+M${Math.floor(cp/100)-1}` :
                      cp < -99 ? `-M${Math.floor(-cp/100)-1}` :
                      `${sign}${cp.toFixed(2)}`;
      evalDisplayEl.textContent = `Eval: ${display}`;
      depthDisplayEl.textContent = `Depth: ${data.depth || '-'}`;
    })
    .catch(() => { /* silent */ });
}

// --- Event Listeners ---

btnNewGame.addEventListener('click', () => { if (!isEngineThinking) startNewGame(); });
btnModalNewGame.addEventListener('click', startNewGame);
modalOverlay.addEventListener('click', (e) => { if (e.target === modalOverlay) hideModal(); });
btnResign.addEventListener('click', resignGame);
btnDraw.addEventListener('click', offerDraw);
btnFlip.addEventListener('click', () => { if (board) board.flip(); });
sideSelect.addEventListener('change', () => { if (!isGameOver) startNewGame(); });

document.addEventListener('keydown', (e) => {
  if (e.target && /^(INPUT|SELECT|TEXTAREA)$/.test(e.target.tagName)) return;
  if (e.key === 'n' && !e.ctrlKey && !e.metaKey) {
    if (!isEngineThinking) startNewGame();
  }
  if (e.key === 'f' && !e.ctrlKey && !e.metaKey) {
    if (board) board.flip();
  }
  if (e.key === 'Escape') hideModal();
});

window.addEventListener('resize', () => { if (board) board.resize(); });

// --- Init ---

function init() {
  if (typeof ChessBoard === 'undefined') {
    showBoardMessage('chessboard.js failed to load. Check web/vendor/chessboard.js.');
    gameStatusEl.textContent = 'Error: chessboard.js not loaded';
    return;
  }
  if (typeof Chess === 'undefined') {
    showBoardMessage('chess.js failed to load. Check web/vendor/chess.min.js.');
    gameStatusEl.textContent = 'Error: chess.js not loaded';
    return;
  }
  if (typeof window.jQuery === 'undefined') {
    showBoardMessage('jQuery failed to load. Check web/vendor/jquery.min.js.');
    gameStatusEl.textContent = 'Error: jQuery not loaded';
    return;
  }
  startEvalUpdates();
  startNewGame();
}

init();

})();
