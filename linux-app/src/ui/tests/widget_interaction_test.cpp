#include <QApplication>
#include <QSignalSpy>
#include <QTest>

#include <iostream>

#include "Check.hpp"
#include "components/GlassButton.hpp"
#include "components/NavRailButton.hpp"
#include "components/ToggleSwitch.hpp"

using namespace wiremic::ui;

int main(int argc, char** argv) {
  QApplication app(argc, argv);

  {
    GlassButton button("Connect", GlassButton::Variant::Primary);
    button.resize(120, 44);
    button.show();
    QSignalSpy spy(&button, &GlassButton::clicked);
    QTest::mouseClick(&button, Qt::LeftButton, Qt::NoModifier,
                       QPoint(60, 22));
    WIREMIC_CHECK(spy.count() == 1);
    std::cout << "GLASS_BUTTON_CLICK_OK\n";
  }

  {
    NavRailButton navButton(":/WireMic/resources/icons/icon_dashboard.svg",
                             "Dashboard");
    navButton.resize(220, 44);
    navButton.show();
    QSignalSpy spy(&navButton, &NavRailButton::clicked);
    QTest::mouseClick(&navButton, Qt::LeftButton, Qt::NoModifier,
                       QPoint(110, 22));
    WIREMIC_CHECK(spy.count() == 1);
    std::cout << "NAV_RAIL_BUTTON_CLICK_OK\n";

    navButton.setSelected(true);
    WIREMIC_CHECK(navButton.isSelected());
    std::cout << "NAV_RAIL_SELECT_STATE_OK\n";
  }

  {
    ToggleSwitch toggle;
    toggle.resize(46, 26);
    toggle.show();
    QSignalSpy spy(&toggle, &ToggleSwitch::toggled);
    WIREMIC_CHECK(!toggle.isChecked());
    QTest::mouseClick(&toggle, Qt::LeftButton, Qt::NoModifier, QPoint(23, 13));
    WIREMIC_CHECK(spy.count() == 1);
    WIREMIC_CHECK(toggle.isChecked());
    const auto args = spy.takeFirst();
    WIREMIC_CHECK(args.at(0).toBool() == true);
    std::cout << "TOGGLE_SWITCH_CLICK_OK\n";
  }

  {
    GlassButton dangerButton("Disconnect", GlassButton::Variant::Danger);
    dangerButton.resize(120, 44);
    dangerButton.show();
    int callCount = 0;
    QObject::connect(&dangerButton, &GlassButton::clicked,
                      [&callCount]() { ++callCount; });
    QTest::mouseClick(&dangerButton, Qt::LeftButton, Qt::NoModifier,
                       QPoint(60, 22));
    QTest::mouseClick(&dangerButton, Qt::LeftButton, Qt::NoModifier,
                       QPoint(60, 22));
    WIREMIC_CHECK(callCount == 2);
    std::cout << "MULTI_CLICK_WIRING_OK\n";
  }

  {
    GlassButton busyButton("Connect", GlassButton::Variant::Primary);
    busyButton.resize(120, 44);
    busyButton.show();
    busyButton.setBusy(true);
    QSignalSpy spy(&busyButton, &GlassButton::clicked);
    QTest::mouseClick(&busyButton, Qt::LeftButton, Qt::NoModifier,
                       QPoint(60, 22));
    WIREMIC_CHECK(spy.count() == 0);
    std::cout << "BUSY_BUTTON_IGNORES_CLICK_OK\n";
  }

  std::cout << "WIDGET_INTERACTION_TESTS_PASSED\n";
  return 0;
}
