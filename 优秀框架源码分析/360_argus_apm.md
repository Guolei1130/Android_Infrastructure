* 仓库地址：https://github.com/Qihoo360/ArgusAPM
* 分析版本：commit id ===> 75ead19ca98a8a1f776688e9df5b572f20c80b12

## 前言

这个框架是360出的一个APM框架，实现较为简单不复杂，但是也可以监控到一些数据。下面主要会针对其中的一些模块采集的数据及实现 简单的看一下。

* gradle编译插件-asm
* Acvitivy声明周期
* 文件监控
* Argus的WatchDog

## gradle编译插件-asm

刚开始，ArgusApm的编译插件还是针对Aspectj的，但是AspectJ有他自己的问题，因此，迁移到了ASM。插件的源码细节不会介绍太多，不难，主要看一下几个ASM相关的。

* OkHttp3MethodAdapter
* WebMethodAdapter

#### OkHttp3MethodAdapter

```kotlin
    override fun visitInsn(opcode: Int) {
        if (isReturn(opcode) && TypeUtil.isOkhttpClientBuild(methodName, desc)) {
            mv.visitVarInsn(ALOAD, 0)
            mv.visitFieldInsn(GETFIELD, "okhttp3/OkHttpClient\$Builder", "interceptors", "Ljava/util/List;")
            mv.visitMethodInsn(Opcodes.INVOKESTATIC, "com/argusapm/android/okhttp3/OkHttpUtils", "insertToOkHttpClientBuilder", "(Ljava/util/List;)V", false)
        }
        super.visitInsn(opcode)
    }
```
在OkHttpClient$Builder调用build方法的时候，通过加入字节码，invoke OkHttpUtils#insertToOkHttpClientBuilder 加入拦截器，统计网络情况

#### WebMethodAdapter

关键代码片段如下

```kotlin
mv.visitLdcInsn("javascript:%s.sendResource(JSON.stringify(window.performance.timing));")
```

可以看到，在ArgusApm中，是通过window.performance.timing去获取webview的一些性能指标的

## Acvitivy声明周期

Activity的声明周期最好的方式是hook掉```Instrumentation```，ArgusApm也不例外。

```java
    private static void hookInstrumentation() throws ClassNotFoundException, NoSuchMethodException, InvocationTargetException, IllegalAccessException, NoSuchFieldException {
        Class<?> c = Class.forName("android.app.ActivityThread");
        Method currentActivityThread = c.getDeclaredMethod("currentActivityThread");
        boolean acc = currentActivityThread.isAccessible();
        if (!acc) {
            currentActivityThread.setAccessible(true);
        }
        Object o = currentActivityThread.invoke(null);
        if (!acc) {
            currentActivityThread.setAccessible(acc);
        }
        Field f = c.getDeclaredField("mInstrumentation");
        acc = f.isAccessible();
        if (!acc) {
            f.setAccessible(true);
        }
        Instrumentation currentInstrumentation = (Instrumentation) f.get(o);
        Instrumentation ins = new ApmInstrumentation(currentInstrumentation);
        f.set(o, ins);
        if (!acc) {
            f.setAccessible(acc);
        }
    }
```

这样，我们就可以在Instrumentation相关生命周期的方法中统计时间等信息了。

```java
    @Override
    public void callActivityOnCreate(Activity activity, Bundle icicle) {
        if (!isActivityTaskRunning()) {
            if (mOldInstrumentation != null) {
                mOldInstrumentation.callActivityOnCreate(activity, icicle);
            } else {
                super.callActivityOnCreate(activity, icicle);
            }
            return;
        }
        if (DEBUG) {
            LogX.d(TAG, SUB_TAG, "callActivityOnCreate");
        }
        long startTime = System.currentTimeMillis();
        if (mOldInstrumentation != null) {
            mOldInstrumentation.callActivityOnCreate(activity, icicle);
        } else {
            super.callActivityOnCreate(activity, icicle);
        }
        ActivityCore.startType = ActivityCore.isFirst ? ActivityInfo.COLD_START : ActivityInfo.HOT_START;

        ActivityCore.onCreateInfo(activity, startTime);
    }
```

## 文件监控

文件监控，比较简单，就是监控文件夹大小。但是ArgusApm的实现方式，并不是最好的， 相对来说 性能差点，建议使用ForkJoin的方式实现

## ArgusApm的WatchDog

```java
    private Handler mHandler = new Handler(Manager.getInstance().getContext().getMainLooper());

    private Runnable runnable = new Runnable() {
        @Override
        public void run() {
            if (null == mHandler) {
                Log.e(TAG, "handler is null");
                return;
            }

            mHandler.post(new Runnable() {
                @Override
                public void run() {
                    mTick++;
                }
            });

            try {
                Thread.sleep(DELAY_TIME);
            } catch (InterruptedException e) {
                e.printStackTrace();
            }

            if (TICK_INIT_VALUE == mTick) {
                String stack = captureStacktrace();
                saveWatchdogInfo(stack);
            } else {
                mTick = TICK_INIT_VALUE;
            }

            AsyncThreadTask.getInstance().executeDelayed(runnable, ArgusApmConfigManager.getInstance().getArgusApmConfigData().funcControl.getWatchDogIntervalTime());
        }
    };
```

在Run方法中，mHandler post 一个消息，去改变mTick的值，如果DELAY_TIME时间内，值没有变化，说明前面某些消息阻塞了一些操作。

## 总结

ArgusApm的实现原理很简单，但仍然有一些地方值得看~

