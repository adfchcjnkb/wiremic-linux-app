#pragma once

#include <QColor>

namespace wiremic::ui::theme {

inline const QColor kBgTop{10, 11, 16};
inline const QColor kBgBottom{16, 17, 22};
inline const QColor kSidebar{14, 15, 20};

inline const QColor kGlassFill{255, 255, 255, 12};
inline const QColor kGlassFillHover{255, 255, 255, 20};
inline const QColor kGlassFillActive{255, 255, 255, 26};
inline const QColor kGlassBorder{255, 255, 255, 22};
inline const QColor kGlassBorderStrong{255, 255, 255, 38};

inline const QColor kCardFill{255, 255, 255, 10};

inline const QColor kAccentStart{124, 147, 255};
inline const QColor kAccentEnd{165, 117, 255};
inline const QColor kSuccess{52, 211, 153};
inline const QColor kDanger{255, 93, 120};
inline const QColor kWarning{251, 191, 36};

inline const QColor kTextPrimary{245, 246, 250};
inline const QColor kTextSecondary{164, 168, 186};
inline const QColor kTextTertiary{108, 112, 134};

inline constexpr int kRadiusSmall = 10;
inline constexpr int kRadiusMedium = 16;
inline constexpr int kRadiusLarge = 22;
inline constexpr int kRadiusXLarge = 26;

}  // namespace wiremic::ui::theme
