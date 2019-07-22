### 前言

一个App不可能完全都是Native代码，Hybrid开发是必不可少的，也是基础架构中比较重要，也比较简单的基础模块。主要有两个方面

* jsbridge
* 速度优化

### jsbridge

jsbridge主要包括两方面，js调用native，native调用js

#### js调用native

js调用native有三种方式

1. addJavascriptInterface
2. shouldOverrideUrlLoading
3. onJsAlert/onJsConfirm/onJsPrompt

#### native调用js

1. loadUrl
2. evaluateJavascript

### 优化优化！无尽的优化

1. WebView预加载，WebView初次加载会消耗几百ms的时间去加载WebView依赖的东西，我们可以把这个时间提前加载，使用空间换时间的优化方式
2. 资源离线化 资源离线化能将网络IO切换到本地IO，网络环境越差，提升越明显
3. WebView缓存 MutableContextWrapper
4. DNS优化[可以参考这个branch](https://github.com/VIPKID-OpenSource/KIDDNS-Android/tree/webview_support)
5. WebView请求并行化
6. 前端预取+后端直出
7. 其他待补充

### evaluateJavascript

如果前端返回的是json，那么需要用[StringEscapeUtils.](https://github.com/apache/commons-lang/blob/master/src/main/java/org/apache/commons/lang3/StringEscapeUtils.java) 进行UNESCAPE_JSON操作。但是这个的初始化是一个相当耗时的操作，所以要提前初始化好。


### 可参考资料

因为公司内部资源不能公开，因此，找一点网上现有的资源~

[Android WebView 性能轻量优化](https://www.jianshu.com/p/39a9832847a6)

WebView更深层次的优化方案，可以参考[百度App-Android H5首屏优化实战](https://mp.weixin.qq.com/s/AqQgDB-0dUp2ScLkqxbLZg)

[0.3s 完成渲染！UC 信息流正文“闪开”优化实践](https://www.infoq.cn/article/9UKos4Xh_6wL4Fh1FOGL)


### 总结一下

1. Native组件提前加载、缓存
2. 前端预取+后端直出
3. 资源离线+信息回填
4. 包裁剪、依赖最小化
5. 请求Native话、图片复用

