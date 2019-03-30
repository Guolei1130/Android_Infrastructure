### 什么是APM、为什么需要APM

APM-Application Performance Management & Monitoring，应用性能管理&监控。

为什么需要APM呢。用户体验很重要，谁会愿意使用一个卡顿的APP呢。作为一个有追求的开发者，APM也是我们基础架构中相当重要的一个点。

### 我们需要利用APM去监控、管理哪些指标

那么，APM主要需要帮助我们监控哪些指标呢。

* 内存方面：内存使用情况、内存泄漏、GC频率、GC停顿时间
* 卡顿方面：FPS情况、方法耗时、启动耗时等等、Activity启动耗时
* CPU：CPU使用频率
* IO：主线程IO block，重复多余的IO、不适当的buffer大小
* 网络方面：RTT、DNS解析、包大小、异常、ping值、网速
* 线程数监控、报警
* 各种性能监控打点，注入网页加载耗时等等
* 其他

下面会针对这些情况，给出一些简单的实现原理。

### 内存方面

### 卡顿方面

### CPU

### IO

### 网络

### 线程监控

### 网页耗时


### 目前一些比较知名的APM库

[360的ArgusApm](https://github.com/Qihoo360/ArgusAPM)
[腾讯的matrix](https://github.com/Tencent/matrix)
