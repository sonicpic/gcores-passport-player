// 按键唤醒守卫状态机的 host 测试:不依赖 SDK,直接验证 key_guard.h 的纯逻辑。
#include <assert.h>
#include <stdio.h>

#include "key_guard.h"

int main(void) {
    key_guard_t g = {0};

    // 未武装:不吞事件,也不改状态。
    assert(!key_guard_consume(&g, false, 0));
    assert(!g.armed);

    // 武装后:普通事件被吞,直到松手或到期。
    key_guard_arm(&g, 1000, 10000);
    assert(g.armed);
    assert(key_guard_consume(&g, false, 1500));   // 长按被吞
    assert(g.armed);
    assert(key_guard_consume(&g, false, 2500));   // 仍被吞
    assert(g.armed);

    // 松手事件撤防,且该事件本身也被吞。
    assert(key_guard_consume(&g, true, 2600));
    assert(!g.armed);
    assert(key_guard_consume(&g, false, 182600)); // RELEASE后180ms的延迟CLICK被吞
    assert(key_guard_consume(&g, false, 352599)); // 尾窗最后1us仍被吞
    assert(!key_guard_consume(&g, false, 352600)); // 尾窗到期后正常派发

    // 唤醒双击的第二次松手会更新尾窗，DOUBLE分类不能泄漏。
    key_guard_arm(&g, 1000, 10000);
    assert(key_guard_consume(&g, true, 2600));
    assert(key_guard_consume(&g, false, 102600)); // 第二次PRESS
    assert(key_guard_consume(&g, true, 272600));
    assert(key_guard_consume(&g, false, 452600)); // 第二次RELEASE后180ms的DOUBLE
    assert(!key_guard_consume(&g, false, 622600));

    // 到期兜底:没有松手事件也能撤防。
    key_guard_arm(&g, 1000, 10000);               // deadline = 11000
    assert(key_guard_consume(&g, false, 11001));  // 到期,吞掉这次
    assert(!g.armed);
    assert(!key_guard_consume(&g, false, 11002));

    // 释放早于初始化:按键驱动启动前就已松手,不会再有 release 事件。
    // 按采样到的物理状态(已松手)同步后立即撤防,下一次长按不再被吞。
    key_guard_arm(&g, 1000, 10000);
    key_guard_sync_released(&g, true);
    assert(!g.armed);
    assert(!key_guard_consume(&g, false, 1500));  // 下一次长按正常派发
    assert(g.tail_until_us==0); // 早于组件初始化的释放不创建尾窗

    // 按住贯穿初始化:采样仍按住,守卫保持武装,直到松手事件。
    key_guard_arm(&g, 1000, 10000);
    key_guard_sync_released(&g, false);           // 仍按住,不撤防
    assert(g.armed);
    assert(key_guard_consume(&g, false, 1500));   // 长按仍被吞
    assert(g.armed);
    assert(key_guard_consume(&g, true, 2600));    // 松手撤防
    assert(!g.armed);
    assert(key_guard_consume(&g, false, 182600)); // 但延迟CLICK仍被吞
    key_guard_sync_released(&g, true);
    assert(!key_guard_consume(&g, false, 182601)); // 初始化同步清除任何旧尾窗

    // 再次武装必须清空旧尾窗，不继承上一次唤醒的时间状态。
    key_guard_arm(&g, 500000, 10000);
    assert(g.armed && g.tail_until_us==0);
    assert(key_guard_consume(&g, false, 510000)); // fallback自身仍被吞
    assert(!key_guard_consume(&g, false, 510001));

    puts("key wake guard state-machine tests: PASS");
    return 0;
}
