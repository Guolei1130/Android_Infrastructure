## KV存储

这里的KV存储方式值得是key-value的存储形式，在Android中，kv存储的主要使用形式是SharedPreferences,除此之外，还有一些其他的形式，如 NoSql。这种存储方式的优点如下：
* 灵活的数据模型
* 易扩展
* 高可用

## SharedPreferences
SharedPreferences是最普通、最常见的一种kv存储形式。但是有以下几点需要注意：
1. 不适合存储大量数据
2. 多进程同步问题
3. 主线程使用卡顿

那么我们有哪些方式可以解决一些sp的问题么。常见的有以下几种方式。

1. 自定义SharedPreferences，替换系统实现 [可以参考这里](https://github.com/AndroidAdvanceWithGeektime/Chapter12)
2. 严格控制sp文件大小
3. 将block的操作放到子线程，可以参考didi booster的sp部分
4. 使用MMKV

## MMKV

mmkv是微信开发的一个高性能、跨平台的kv存储组件，性能非常强，且进程同步，不会出现数据丢失，是一个非常优秀的组件。个人十分推荐。

## NOSQL

可能很多同学在Android上没有使用过NoSql，NoSql在Android上也比较小众，由于MMKV的出现，我个人倾向kv使用MMKV。

## 总结

由于MMKV的出现，我个人倾向在kv存储这一块，使用MMKV。