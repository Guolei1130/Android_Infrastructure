### 前言

在开始研究APM之前，我们需要先了解AspectJ、ASM、Gradle插件、Native Hook等知识，不了解的同学可以自行查找资料

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
5. 方法耗时

#### 应用启动耗时

在开发过程中，我们可以很轻松的拿到应用启动的时长，那么，在线上我们怎么做呢。我们可以粗略的认为Application#attachBaseContext为起始时间，第一个Activity的onResume方法结束为终止时间，计算出来。

attachBaseContext方法的起始时间，我们可以通过AOP或者ASM插入代码的方式去拿到。

```


  private static final String ATTACH_METHOD = "execution(* android.app.Application.attachBaseContext(..))";


  @Pointcut(ATTACH_METHOD)
  public void attachBaseContext() {

  }

  @Around("attachBaseContext()")
  public Object attachBaseContextAdvice(ProceedingJoinPoint joinPoint) {
    //TODO 多进程
    Object result = null;
    try {
      Application application = (Application) joinPoint.getTarget();
      long startTime = SystemClock.elapsedRealtime();
      result = joinPoint.proceed();
      String applicaitonName = application.getClass().getCanonicalName();
      Signature signature = joinPoint.getSignature();
      String sign = "";
      if (signature != null) {
        sign = signature.toString();
      }

      if (!TextUtils.isEmpty(applicaitonName) && !TextUtils.isEmpty(sign) && sign
          .contains(applicaitonName)) {
        //统计时间
      }
    } catch (Exception e) {
      e.printStackTrace();
    } catch (Throwable throwable) {
      throwable.printStackTrace();
    }

    return result;
  }

```
Activity的时间，看下面

#### Activity的生命周期时间

我们可以替换Instrumation去很轻松的拿到Activity的生命周期耗时


```
public class HookInstrumentation {

  public static void hook(Application application) {
    // TODO: 2019/4/11 AOSP看源码，看是否有兼容性问题
    Context context = application.getBaseContext();
    try {
      Class contextImplClz = HiddenReflectHelper.forName("android.app.ContextImpl");
      Field mMainThread = HiddenReflectHelper.getDeclaredField(contextImplClz, "mMainThread");
      mMainThread.setAccessible(true);
      Object activityThread = mMainThread.get(context);
      Class activityThreadClz = HiddenReflectHelper.forName("android.app.ActivityThread");
      Field mInstrumentationField = HiddenReflectHelper
          .getDeclaredField(activityThreadClz, "mInstrumentation");
      mInstrumentationField.setAccessible(true);
      Instrumentation origin = (Instrumentation) mInstrumentationField.get(activityThread);
      Instrumentation hookIn = new SauronInstrumentation(origin);

      mInstrumentationField.set(activityThread, hookIn);
      Method mThreadMethod = HiddenReflectHelper
          .getDeclaredMethod(Instrumentation.class, "basicInit",
              activityThreadClz);
      mThreadMethod.setAccessible(true);
      mThreadMethod.invoke(hookIn, activityThread);
    } catch (Exception e) {
      Log.e(Constants.INSTANCE.getTAG(), "HookInstrumentation#hookLayoutInflate " );
      e.printStackTrace();
    }
  }

}

```

```
  override fun callActivityOnCreate(activity: Activity, icicle: Bundle?) {
    val start = SystemClock.elapsedRealtime()
    activity.traceBefore(SauronTrace.ON_CREATE)
    super.callActivityOnCreate(activity, icicle)
    activity.traceAfter(SauronTrace.ON_CREATE, start)
  }

  override fun callActivityOnStart(activity: Activity) {
    val start = SystemClock.elapsedRealtime()
    activity.traceBefore(SauronTrace.ON_START)
    super.callActivityOnStart(activity)
    activity.traceAfter(SauronTrace.ON_START, start)
  }

  override fun callActivityOnResume(activity: Activity) {
    val start = SystemClock.elapsedRealtime()
    activity.traceBefore(SauronTrace.ON_RESUME)
    super.callActivityOnResume(activity)
    activity.traceAfter(SauronTrace.ON_RESUME,start)
  }

  override fun callActivityOnDestroy(activity: Activity) {
    val start = SystemClock.elapsedRealtime()
    activity.traceBefore(SauronTrace.ON_DESTROY)
    super.callActivityOnDestroy(activity)
    activity.traceAfter(SauronTrace.ON_DESTROY, start)
  }

```


#### Fragment的耗时

因为现在大多是在使用support包，因此，我们使用AOP或者ASM都可以拿到。

```
  private static final String ONATTACH = "execution(* android.support.v4.app.Fragment.onAttach(android.content.Context))";
  
  @Pointcut(ONATTACH)
  public void onAttach() {

  }

  // TODO: 2019/4/25 attach方法不准确，但是为了能在线上统计到一些数据，暂时使用这种方式
  @Around("onAttach()")
  public Object onAttachAdvice(ProceedingJoinPoint joinPoint) {
    Object result = null;
    try {
      long startTime = SystemClock.elapsedRealtime();
      result = joinPoint.proceed();
      //统计时间
    } catch (Exception e) {
      e.printStackTrace();
    } catch (Throwable throwable) {
      throwable.printStackTrace();
    }

    return result;
  }

```

#### inflate耗时

这个功能，目前的实现方式只能在dev环境做，利用Hook框架去hook inflate方法，插桩比较前后时间即可。笔者现在使用的是AndHook

```

  private fun hookLayoutInflate() {
    XposedHelpers.findAndHookMethod(LayoutInflater::class.java,
        "inflate",
        Int::class.javaPrimitiveType, ViewGroup::class.java, Boolean::class.javaPrimitiveType,
        object : XC_MethodHook() {
          internal var startTime: Long = 0

          @Throws(Throwable::class)
          override fun beforeHookedMethod(param: XC_MethodHook.MethodHookParam) {
            startTime = SystemClock.elapsedRealtime()
            super.beforeHookedMethod(param)
          }

          @Throws(Throwable::class)
          override fun afterHookedMethod(param: XC_MethodHook.MethodHookParam) {
            super.afterHookedMethod(param)
            val useTime = SystemClock.elapsedRealtime() - startTime
//            if (useTime > 300) {
            val name = context.resources.getResourceName(param.args[0] as Int)
            if (!name.startsWith("android:layout") && !inBlackList(name)) {
              //上报
            }
//            }
          }
        })
  }

```

#### 方法耗时

在编译过程中，前后插入统计代码即可，难度不高。可参考Matrix

```
  private var inject: Boolean = false

  init {
    className = this.className?.replace("/",".")
  }

  override fun visitAnnotation(desc: String?, visible: Boolean): AnnotationVisitor {
    if (desc!!.contains("xxx")) {
      inject = true
    }
    return super.visitAnnotation(desc, visible)
  }

  override fun onMethodEnter() {
    if (inject) {
		//注入代码
    }
    super.onMethodEnter()
  }

  override fun onMethodExit(opcode: Int) {
    if (inject) {
		//注入代码
    }
    super.onMethodExit(opcode)
  }

```


### 内存方面

1. 内存抖动、GC频繁
2. 内存峰值监控报警
3. 页面启动内存增量报警
4. Bitmap过大报警、Bitmap超过View大小报警

#### GC频繁

每隔1S采集一下GC数据，前后对比，将超过阀值的情况对应的堆栈打出来，即可。采集代码如下

```
fun getGcInfo(): GcInfo {
    //TODO 可能会被移除
    val start = SystemClock.elapsedRealtime()
    val allocCount = Debug.getGlobalAllocCount()
    val allocSize = Debug.getGlobalAllocSize()
    var gcCount = Debug.getGlobalGcInvocationCount()

    val gcInfo: GcInfo
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
      gcCount = Debug.getRuntimeStat("art.gc.gc-count").toInt()
      val gcTime = Debug.getRuntimeStat("art.gc.gc-time").toInt()
      val blockGcCount = Debug.getRuntimeStat("art.gc.blocking-gc-count").toInt()
      val blockGcTime = Debug.getRuntimeStat("art.gc.blocking-gc-time").toInt()
      gcInfo = GcInfo(allocCount.toLong(), allocSize.toLong(), gcCount.toLong(), gcTime.toLong(),
          blockGcCount.toLong(), blockGcTime.toLong())
    } else {
      gcInfo = GcInfo(allocCount.toLong(), allocSize.toLong(), gcCount.toLong())
    }
    return gcInfo
  }

  fun getMemoryInfo(): MemoryInfo {
    assert(Thread.currentThread() != Looper.getMainLooper().thread)
    val memoryInfo: Debug.MemoryInfo = Debug.MemoryInfo()
    Debug.getMemoryInfo(memoryInfo)
    return MemoryInfo(memoryInfo.totalPss,
        memoryInfo.dalvikPss,
        memoryInfo.nativePss,
        memoryInfo.otherPss)
  }


```

#### 内存峰值报警

获取内存使用率代码如下

```
  fun getProporation(): Int {
    val javaMax = mRuntime.maxMemory()
    val javaTotal = mRuntime.totalMemory()
    val javaUse = javaTotal.minus(mRuntime.freeMemory())
    return (javaUse.toFloat() / javaMax.toFloat() * 100).toInt()
  }

```

#### 页面启动内存增量报警

Activity启动前后，GC、内存数据对比，超过阀值报警上传。基本上就是上面说的那些

#### Bitmap过大


### 卡顿方面

1. Block监控 
2. FPS流畅度监控 

#### Block监控
>可以参考markzhai的开源库

#### FPS流畅度

```Choreographer.getInstance().postFrameCallback(this)```

```
  /**
   * 计算fps的值
   */
  private fun calculateFps(frameTimeNanos: Long) {
    frameCount++
    if (lastTimeNanos == 0L) {
      lastTimeNanos = frameTimeNanos
    }
    if ((frameTimeNanos - lastTimeNanos) / 1000000000.0F >= 1) {
      lastTimeNanos = frameTimeNanos
      //流畅度，要上报么？
      frameCount = 0
    }
  }

```

### CPU

1. CPU过载

### IO

我们可以通过GOT/PLT等hook方式，hook掉open、write、read、close等方法，统计一些东西。可以参考matrix的io模块。

1. 主线程IO
    并不是所有的主线程io都需要被上报，当主线程的读写时间和读写次数超过阀值之后，就可以上报。
2. 不合理的buffer
	close之前，读写数据总量大小/读写次数 可以看做是buffer大小，或者直接hook Libcore.io也行(参考httpdns的实现方式)
3. 重复读
	对同一文件多次open、多次读
4. Closeable对象未关闭

#### Closeable对象未关闭
>Android 框架提供的 StrictMode 也支持 Closeable Leak Detect ，框架级的监控自然最合适的，所以决定借 StrictMode 东风。 稍微看下 StrictMode 的源码，发现主要依赖一个工具类 dalvik.system.CloseGuard 来实现。这里依然举 FileInputStream 的例子，看怎么发现没有 close 。

>```
//open
public FileInputStream(File file)...{
    ...
    //CloseGuard
    guard.open("close");
    ...
}
//close
public void close()...{
	...
    guard.close();
    ...
} 
//finalize
protected void finalize() throws IOException {
	...
    if (guard != null) {
        guard.warnIfOpen();
    }
    ...
}      
```

>以上看到， GC 准备回收这个 FileInputStream ，会调用 guard.warnIfOpen 。再看下 guard.warnIfOpen 做了什么, 同时还有 guard.close 和 guard.open 。

```
public void open(String closer) {
	...
    allocationSite = new Throwable(message);
    ...
}

public void close() {
    allocationSite = null;
}

public void warnIfOpen() {
    if (allocationSite == null || !ENABLED) {
        return;
    }
    ...
    REPORTER.report(message, allocationSite);
}
```
>看到这里，就清晰了，warnIfOpen 时如果没 close ，就 REPORTER.report 。

>到这里大概知道 Closeable Leak 怎么实现了，那怎么利用它呢，再看下 REPORTER ：

```
//静态变量
private static volatile Reporter REPORTER = new DefaultReporter();
//接口
public static interface Reporter {
        public void report (String message, Throwable allocationSite);
}
```
>看到这里， hook 点非常清晰，把 REPORTER 换掉就行了。找到了 hook 点，那么就容易了：

>利用反射，把 warnIfOpen 那个 ENABLED 值设为 true
利用动态代理，把 REPORTER 替换成我定义的 proxy
这时，框架层的代码只要发现 closeable leak 问题就会 report 给 IOCanary 了。当然框架层很多代码都用了 CloseGuard ，就可以发现比如文件资源没 close ， Cursor 没有 close 等等，一下子满足了好多愿望。

具体的代码实现，可以参考Matrix或者OkHttp

### 网络

1. 流量监控
2. RTT监控
3. 成功率监控

### 线程监控

1. 线程数阀值报警
2. 线程名约束

获取线程信息代码如下

```
object ThreadUtils {

  fun getThreads(): ThreadInfo? {
    return try {
      val field = ThreadGroup::class.java.getDeclaredField("systemThreadGroup")
      field.isAccessible = true
      val threadGroup = field.get(null) as ThreadGroup
      var count = threadGroup.activeCount()
      val threads: Array<Thread?> = Array(count + count / 2) { null }
      count = threadGroup.enumerate(threads)
      val list = ArrayList<String>()
      threads.forEach {
        if (it != null) {
          list.add(it.name)
        }
      }
      ThreadInfo(count, list)
    } catch (e: Exception) {
      e.printStackTrace()
      val pid = Process.myPid()
      return try {
        val dir = File("/proc/$pid/task")
        val list = ArrayList<String>()
        dir.listFiles().forEach {
          if (it != null) {
            list.add(getName(it))
          }
        }
        ThreadInfo(list.size, list)
      } catch (e: Exception) {
        e.printStackTrace()
        ThreadInfo(0, null)
      }
    }
  }

  private fun getName(file: File): String {
    try {
      val statFile = File(file, "stat")
      val randomAccessFile = RandomAccessFile(statFile, "r")
      while (true) {
        val content = randomAccessFile.readLine()
        if (!TextUtils.isEmpty(content)) {
          return content.split(Regex("\\s+"))[1]
              .replace("(","")
              .replace(")","")
        }
      }
    } catch (e: Exception) {
      e.printStackTrace()
      return ""
    }
  }
}

```

#### 线程名约束

可以通过编译时lint或者是AndHook的方式去做，这里是AndHook的方式

```
private fun hookStart() {
    XposedHelpers.findAndHookMethod(Thread::class.java,
        "start",
        object : XC_MethodHook() {
          @Throws(Throwable::class)
          override fun beforeHookedMethod(param: XC_MethodHook.MethodHookParam) {
            val thread = param.thisObject as? Thread
            thread?.let {
              if (thread.name.startsWith("Thread-") || thread.name.contains(Regex("\\s+"))
                  || thread.name.startsWith("Timer-")) {
                ThreadLintInfo(thread.name, StackUtils.getCurrentStack()).report(Constants.TYPE_THREAD_LINT)
              }
            }
            super.beforeHookedMethod(param)
          }
        }
    )
  }

```

### 网页耗时


### 目前一些比较知名的APM库

[360的ArgusApm](https://github.com/Qihoo360/ArgusAPM)
[腾讯的matrix](https://github.com/Tencent/matrix)
