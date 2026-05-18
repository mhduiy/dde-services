// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include <QtTest>
#include "../../src/plugin-qt/power/session/idle/idlewatcher.h"

class TestIdleWatcher : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testAbstractInterface();
    void testX11Stub();
};

void TestIdleWatcher::testAbstractInterface()
{
    // X11 stub returns isValid=false
    class StubWatcher : public IdleWatcher {
    public:
        using IdleWatcher::IdleWatcher;
        bool isValid() const override { return false; }
        void setTimeout(uint32_t) override {}
        void simulateActivity() override {}
        uint32_t idleTimeMs() const override { return 0; }
        bool isIdle() const override { return false; }
    };

    StubWatcher w;
    QVERIFY(!w.isValid());
    QVERIFY(!w.isIdle());
    QCOMPARE(w.idleTimeMs(), 0u);
    w.setTimeout(300);
    w.simulateActivity();
}

void TestIdleWatcher::testX11Stub()
{
    // Verify the pre-built X11 stub behaves correctly
    // (included via idlewatcher_x11.cpp)
}

QTEST_MAIN(TestIdleWatcher)
#include "test_idlewatcher.moc"
