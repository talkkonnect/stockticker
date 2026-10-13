#pragma once
// Profit/loss maths for long and short positions.
// SET prices have at most 2 decimals, so work in whole satang (0.01 THB):
// floats like 9.35 are not exact and the error grows with large share counts.

#include <math.h>
#include "settings.h"

inline int64_t toSatang(float price) { return llround((double)price * 100.0); }

// Price move in the position's favour, per share, in satang.
inline int64_t positionGainSatang(const Position &p, float price) {
  return p.isShort ? toSatang(p.entry) - toSatang(price) : toSatang(price) - toSatang(p.entry);
}

// THB, exact to the satang.
inline double positionPL(const Position &p, float price) {
  return (double)(positionGainSatang(p, price) * (int64_t)p.volume) / 100.0;
}

inline double positionPLPercent(const Position &p, float price) {
  return p.entry > 0 ? (double)positionGainSatang(p, price) / toSatang(p.entry) * 100.0 : 0;
}

inline double positionPLAtTarget(const Position &p) {
  return p.target > 0 ? positionPL(p, p.target) : 0;
}

// Long: price has risen to the selling price. Short: price has fallen to the buy-back price.
inline bool targetReached(const Position &p, float price) {
  if (p.target <= 0 || price <= 0) return false;
  return p.isShort ? toSatang(price) <= toSatang(p.target) : toSatang(price) >= toSatang(p.target);
}
