### 前言

在开始研究APM之前，我们需要先了解AspectJ、Native Hook等知识，不了解的同学可以自行查找资料

### 什么是APM、为什么需要APM

APM-Application Performance Management & Monitoring，应用性能管理&监控。

为什么需要APM呢。用户体验很重要，谁会愿意使用一个卡顿的APP呢。作为一个有追求的开发者，APM也是我们基础架构中相当重要的一个点。

### 我们需要利用APM去监控、管理哪些指标

那么，APM主要需要帮助我们监控哪些指标呢。

* 启动耗时
* 内存方面：内存使用情况、内存泄漏、GC频率、GC停顿时间
* 卡顿方面：FPS情况、方法耗时、启动耗时等等、Activity启动耗时
* CPU：CPU使用频率
* IO：主线程IO block，重复多余的IO、不适当的buffer大小
* 网络方面：RTT、DNS解析、包大小、异常、ping值、网速
* 线程数监控、报警
* 各种性能监控打点，注入网页加载耗时等等
* 其他

下面会针对这些情况，给出一些简单的实现原理。

### 耗时

耗时主要包括以下几方面

1. 应用启动耗时
2. Activity启动耗时
3. Fragment启动耗时
4. xml inflate耗时


### 内存方面

1. 内存抖动、GC频繁
2. 内存峰值监控报警
3. 页面启动内存增量报警
4. Bitmapf过大报警、Bitmap超过View大小报警

### 卡顿方面

1. Block监控
2. FPS流畅度监控

### CPU

1. CPU过载

### IO

1. 主线程IO
2. 不合理的buffer

### 网络

1. 流量监控
2. RTT监控
3. 成功率监控

### 线程监控

1. 线程数阀值报警
2. 线程名约束

### 网页耗时


### 目前一些比较知名的APM库

[360的ArgusApm](https://github.com/Qihoo360/ArgusAPM)
[腾讯的matrix](https://github.com/Tencent/matrix)
