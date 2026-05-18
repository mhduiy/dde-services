// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include <QtTest>
#include "../../src/plugin-qt/power/session/screen/screencontroller.h"

class TestScreenController : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testConvenienceMethods();
    void testX11Stub();
};

class StubSC : public ScreenController {
public:
    using ScreenController::ScreenController;
    bool isValid() const override { return true; }
    int outputCount() const override { return m_count; }
    Mode mode(int i) const override { return i < m_count ? m_modes[i] : On; }
    void setMode(int i, Mode m) override { if (i < m_count) { m_modes[i] = m; Q_EMIT modeChanged(i, m); } }
    int m_count = 0;
    Mode m_modes[4] = {On, On, On, On};
};

void TestScreenController::testConvenienceMethods()
{
    StubSC sc;
    // isAllOff on empty should return false
    QCOMPARE(sc.isAllOff(), false);

    sc.m_count = 2;
    QCOMPARE(sc.isAllOff(), false);
    sc.setAllModes(ScreenController::Off);
    QCOMPARE(sc.isAllOff(), true);
    sc.setAllModes(ScreenController::On);
    QCOMPARE(sc.isAllOff(), false);
}

void TestScreenController::testX11Stub()
{
}

QTEST_MAIN(TestScreenController)
#include "test_screencontroller.moc"
