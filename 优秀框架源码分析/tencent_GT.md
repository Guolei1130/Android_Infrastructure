* 仓库地址：https://github.com/Tencent/GT
* 分析版本：commit id ===> 6e115aa7927441a7348f747b1316d5f4c4b4241a

## GT
GT是腾讯较早出的一款APM监控工具，但是不适合线上使用，采集的指标也比较少，不过还是有一些地方指的我们学习。主要性能指标如下:

* 基础数据 CPU 内存 等
* Activity、Fragment声明周期耗时
* 页面流畅度
* 数据库的一些操作
* logcat日志
* view的一些操作


## yhook还是AndHook
yhook是GT中的hook框架，可以提供方法AOP的功能，如果大家现在想实现类似的功能，我推荐使用lody的AndHook

## 声明周期耗时

Activity的声明周期耗时，只需要hook Instrumentation，在相关的声明周期方法前后插入统计代码即可。

Fragment的生命周期耗时，可以hook fragment相关的方法。

```java
    @HookAnnotation(
            className = "android.app.Instrumentation",
            methodName = "execStartActivity",
            methodSig = "(Landroid/content/Context;" +
                    "Landroid/os/IBinder;" +
                    "Landroid/os/IBinder;" +
                    "Landroid/app/Activity;" +
                    "Landroid/content/Intent;" +
                    "ILandroid/os/Bundle;)" +
                    "Landroid/app/Instrumentation$ActivityResult;"
    )
    public static ActivityResult execStartActivity(Object thiz, Context who,
                                                   IBinder contextThread, IBinder token,
                                                   Activity target, Intent intent,
                                                   int requestCode, Bundle options) {
        GTRLog.e(TAG,"Instrumentation.execStartActivity");
        long start = System.currentTimeMillis();
        Instrumentation.ActivityResult activityResult =
                execStartActivity_backup(thiz, who, contextThread, token, target, intent, requestCode, options);
        long end = System.currentTimeMillis();

        GTRClient.pushData(new StringBuilder()
                .append("Instrumentation.execStartActivity")
                .append(GTConfig.separator).append(start)
                .append(GTConfig.separator).append(end)
                .toString());

        return activityResult;
    }
```

```
    @HookAnnotation(
            className = "android.support.v4.app.Fragment",
            methodName = "performCreate",
            methodSig = "(Landroid/os/Bundle;)V")
    public static void performCreate(Object thiz, Bundle savedInstanceState) {
        GTRLog.e(TAG,"performCreate");
        long start = System.currentTimeMillis();
        performCreate_backup(thiz, savedInstanceState);
        long end = System.currentTimeMillis();

        String activityClassName = "";
        String activityHashCode = "";
        String fragmentClassName = "";
        String fragmentHashCode = "";
        Object fragment = thiz;

        if (fragment instanceof android.support.v4.app.Fragment) {
            fragmentClassName = ((android.support.v4.app.Fragment)fragment).getClass().getName();
            fragmentHashCode = "" + thiz.hashCode();
            Activity activity = ((android.support.v4.app.Fragment)fragment).getActivity();
            if (activity != null) {
                activityClassName = activity.getClass().getName();
                activityHashCode = "" + activity.hashCode();
            }
        }
        GTRClient.pushData(new StringBuilder()
                .append("FragmentV4.performCreate")
                .append(GTConfig.separator).append(activityClassName)
                .append(GTConfig.separator).append(activityHashCode)
                .append(GTConfig.separator).append(fragmentClassName)
                .append(GTConfig.separator).append(fragmentHashCode)
                .append(GTConfig.separator).append(start)
                .append(GTConfig.separator).append(end)
                .toString());
    }
```

## 页面流畅度

页面流畅度是通过Choreographer的postFrameCallback实现的。具体原理这里不说了。

## 数据库
数据库主要对下面几个方法进行hook，拿配置以及耗时情况
* beginTransaction
* endTransaction
* enableWriteAheadLogging 数据库的WAL模式
* openDatabase
* rawQueryWithFactory
* SQLiteStatement#execute 、executeInsert 、executeUpdateDelete


## logcat
logcat日志，通过```adb logcat```，将我们的日志输出在文件即可。

## view
view主要hook 
* inflate 查找出耗时的xml
* ViewGroup#dispatchDraw
* View#dispatchDraw

## 总结

从原理上并不复杂，不过还是能够给我们一些启示。想了解APM更多设计及实现方面的东西，可以看这个repo下的APM。
