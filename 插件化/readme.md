
在不少应用中，插件化已经成为必不可少的一环。尤其是一些平台化、工具化的应用中。在一个成熟的app中，我们可以把一些使用频率较低或者非必要模块作为插件，使用时按需加载，一方面可以减少apk体积，另一方面这部分功能还可以做到动态升级。

如果现阶段还需要研究插件化的话，本人建议研究三个框架。

* 滴滴的VirtualApk
* 360的Replugin
* 掌阅的ZeusPlugin


下面为很早之前写的一篇文章的内容。

### 前言

Android P preview版本中，已限制对@hide api的反射调用，具体的原理可以阅读[Android P调用隐藏API限制原理](https://mp.weixin.qq.com/s/sktB0x5yBexkn4ORQ1YofA)这篇文章。由于最近团队分享也在分享插件化、热修复相关的东西。因此，写一篇文章，好好记录一下。

### 准备知识

* 反射、动态代理
* Android中的几个相关的ClassLoader,注意PathClassLoader在ART虚拟机上是可以加载未安装的APK的，Dalvik虚拟机则不可以。
* Android中四大组件的相关原理
* PackageManagerServer
* 资源加载、资源打包
* 其他

**文章中所涉及到的代码均通过Nexus 5(dalvik虚拟机) Android 6.0版本的测试**

文章中所涉及到的一切资源都在[这个仓库下](https://github.com/Guolei1130/blog_resource/tree/master/source_code/plugin_about)


特别说明，本博客不会特别解释过多原理性的东西。如果读者不具备相关的知识储备，建议先阅读weishu和gityuan两位大神的博客,资源打包的知识可以阅读 老罗的博客。

* [Weishu's Notes](http://weishu.me/)
* [gityuan](http://gityuan.com/)

### Activity的插件化

首先需要说明一点的是，启动一个完全没有在AndroidManifest注册的Activity是不可能的。因为在启动的过程中，存在一个校验的过程，而这个校验则是由PMS来完成的，这个我们无法干预。因此，Activity的插件化方案大多使用占坑的思想。不同的是如何在检验之前替换，在生成对象的时候还原。就目前来看，有两种比较好方案：

* Hook Instrumentation方案
* 干预startActivity等方法，干预ClassLoader findClass的方案


这里说一下Hook Instrumentation方法。根据上面提到的想法，我们需要在先绕过检查，那么，我们如何绕过检查呢？通过分析Activity的启动流程会发现，在Instrumentation#execStartActivity中，会有个checkStartActivityResult的方法去检查错误，因此，我们可以复写这个方法，让启动参数能通过系统的检查。那么，我们如何做呢？首先，我们需要检查要启动的Intent能不能匹配到，匹配不到的话，将ClassName修改为我们预先在AndroidManifest中配置的占坑Activity，并且吧当前的这个ClassName放到当前intent的extra中，以便后续做恢复，看下代码。

```
public ActivityResult execStartActivity(
    Context who, IBinder contextThread, IBinder token, Activity target,
    Intent intent, int requestCode, Bundle options) {
    List<ResolveInfo> infos = mPackageManager.queryIntentActivities(intent, PackageManager.MATCH_ALL);
    if (infos == null || infos.size() == 0) {
        //没查到，要启动的这个没注册
        intent.putExtra(TARGET_ACTIVITY, intent.getComponent().getClassName());
        intent.setClassName(who, "com.guolei.plugindemo.StubActivity");
    }

    Class instrumentationClz = Instrumentation.class;
    try {
        Method execMethod = instrumentationClz.getDeclaredMethod("execStartActivity",
        Context.class, IBinder.class, IBinder.class, Activity.class, Intent.class, int.class, Bundle.class);
        return (ActivityResult) execMethod.invoke(mOriginInstrumentation, who, contextThread, token,
            target, intent, requestCode, options);
    } catch (Exception e) {
        e.printStackTrace();
    }
    return null;
}
```

我们绕过检测了，现在需要解决的问题是还原，我们知道，系统启动Activity的最后会调用到ActivityThread里面，在这里，会通过Instrumentation#newActivity方法去反射构造一个Activity的对象，因此，我们只需要在这里还原即可。代码如下：

```
@Override
public Activity newActivity(ClassLoader cl, String className, Intent intent) throws InstantiationException,
    IllegalAccessException, ClassNotFoundException {
    if (!TextUtils.isEmpty(intent.getStringExtra(TARGET_ACTIVITY))) {
        return super.newActivity(cl, intent.getStringExtra(TARGET_ACTIVITY), intent);
    }
    return super.newActivity(cl, className, intent);
}
```

一切准备就绪，我们最后的问题是，如何替换掉系统的Instrumentation。要替换掉也简单，替换掉ActivityThread中的mInstrumentation字段即可。

```
private void hookInstrumentation() {
    Context context = getBaseContext();
    try {
        Class contextImplClz = Class.forName("android.app.ContextImpl");
        Field mMainThread = contextImplClz.getDeclaredField("mMainThread");
        mMainThread.setAccessible(true);
        Object activityThread = mMainThread.get(context);
        Class activityThreadClz = Class.forName("android.app.ActivityThread");
        Field mInstrumentationField = activityThreadClz.getDeclaredField("mInstrumentation");
        mInstrumentationField.setAccessible(true);
        mInstrumentationField.set(activityThread,
        new HookInstrumentation((Instrumentation) mInstrumentationField.get(activityThread),
            context.getPackageManager()));
    } catch (Exception e) {
        e.printStackTrace();
        Log.e("plugin", "hookInstrumentation: error");
    }
}
```

这样，我们就能启动一个没有注册在AndroidManifest文件中的Activity了，但是这里要注意一下，由于我们这里使用的ClassLoader是宿主的ClassLoader，这样的话，我们需要将插件的dex文件添加到我们宿主中。这一点很重要。有一些多ClassLoader架构的实现，这里的代码需要变下。

### Service的插件化

启动一个未注册的Service，并不会崩溃退出，只不过有点警告。并且，service启动直接由ContextImpl交给AMS处理了，我们看下代码。

```
private ComponentName startServiceCommon(Intent service, UserHandle user) {
    try {
        validateServiceIntent(service);
        service.prepareToLeaveProcess(this);
        ComponentName cn = ActivityManagerNative.getDefault().startService(
            mMainThread.getApplicationThread(), service, service.resolveTypeIfNeeded(
            getContentResolver()), getOpPackageName(), user.getIdentifier());
        if (cn != null) {
            if (cn.getPackageName().equals("!")) {
                throw new SecurityException(
                    "Not allowed to start service " + service
                    + " without permission " + cn.getClassName());
            } else if (cn.getPackageName().equals("!!")) {
                throw new SecurityException(
                    "Unable to start service " + service
                    + ": " + cn.getClassName());
            }
        }
        return cn;
    } catch (RemoteException e) {
        throw e.rethrowFromSystemServer();
    }
}
```

并且创建对象的过程不由Instrumentation来创建了，而直接在ActivityThread#handleCreateService反射生成。那么，Activity的思路我们就不能用了，怎么办呢？既然我们无法做替换还原，那么，我们可以考虑代理，我们启动一个真实注册了的Service，我们启动这个Service，并让这个Service，就按照系统服务Service的处理，原模原样的处理我们插件的Service。

说做就做，我们以startService为例。我们首先要做的是，hook掉AMS，因为AMS启动service的时候，假如要启动插件的Service，我们需要怎么做呢？把插件service替换成真是的代理Service，这样，代理Service就启动起来了，我们在代理Service中，构建插件的Service，并调用attach、onCreate等方法。

Hook AMS代码如下：

```
private void hookAMS() {
    try {
        Class activityManagerNative = Class.forName("android.app.ActivityManagerNative");
        Field gDefaultField = activityManagerNative.getDeclaredField("gDefault");
        gDefaultField.setAccessible(true);
        Object origin = gDefaultField.get(null);
        Class singleton = Class.forName("android.util.Singleton");
        Field mInstanceField = singleton.getDeclaredField("mInstance");
        mInstanceField.setAccessible(true);
        Object originAMN = mInstanceField.get(origin);
        Object proxy = Proxy.newProxyInstance(Thread.currentThread().getContextClassLoader(),
            new Class[]{Class.forName("android.app.IActivityManager")},
            new ActivityManagerProxy(getPackageManager(),originAMN));
        mInstanceField.set(origin, proxy);
        Log.e(TAG, "hookAMS: success" );
    } catch (Exception e) {
        Log.e(TAG, "hookAMS: " + e.getMessage());
    }
}
```

我们在看一下ActivityManagerProxy这个代理。

```
@Override
public Object invoke(Object proxy, Method method, Object[] args) throws Throwable {
    if (method.getName().equals("startService")) {
        Intent intent = (Intent) args[1];
        List<ResolveInfo> infos = mPackageManager.queryIntentServices(intent, PackageManager.MATCH_ALL);
        if (infos == null || infos.size() == 0) {
            intent.putExtra(TARGET_SERVICE, intent.getComponent().getClassName());
            intent.setClassName("com.guolei.plugindemo", "com.guolei.plugindemo.StubService");
        }

    }
    return method.invoke(mOrigin, args);
}
```

代码很清晰、也很简单，不需要在做多余的了，那么，我们看下代理Service是如何启动并且调用我们的插件Service的。


```
@Override
public int onStartCommand(Intent intent, int flags, int startId) {
    Log.e(TAG, "onStartCommand: stub service ");
    if (intent != null && !TextUtils.isEmpty(intent.getStringExtra(TARGET_SERVICE))) {
    //启动真正的service
        String serviceName = intent.getStringExtra(TARGET_SERVICE);
        try {
            Class activityThreadClz = Class.forName("android.app.ActivityThread");
            Method getActivityThreadMethod = activityThreadClz.getDeclaredMethod("getApplicationThread");
            getActivityThreadMethod.setAccessible(true);
            //获取ActivityThread
            Class contextImplClz = Class.forName("android.app.ContextImpl");
            Field mMainThread = contextImplClz.getDeclaredField("mMainThread");
            mMainThread.setAccessible(true);
            Object activityThread = mMainThread.get(getBaseContext());
            Object applicationThread = getActivityThreadMethod.invoke(activityThread);
            //获取token值
            Class iInterfaceClz = Class.forName("android.os.IInterface");
            Method asBinderMethod = iInterfaceClz.getDeclaredMethod("asBinder");
            asBinderMethod.setAccessible(true);
            Object token = asBinderMethod.invoke(applicationThread);
            //Service的attach方法
            Class serviceClz = Class.forName("android.app.Service");
            Method attachMethod = serviceClz.getDeclaredMethod("attach",
                Context.class, activityThreadClz, String.class, IBinder.class, Application.class,           Object.class);
            attachMethod.setAccessible(true);
            Class activityManagerNative = Class.forName("android.app.ActivityManagerNative");
            Field gDefaultField = activityManagerNative.getDeclaredField("gDefault");
            gDefaultField.setAccessible(true);
            Object origin = gDefaultField.get(null);
            Class singleton = Class.forName("android.util.Singleton");
            Field mInstanceField = singleton.getDeclaredField("mInstance");
            mInstanceField.setAccessible(true);
            Object originAMN = mInstanceField.get(origin);
            Service targetService = (Service) Class.forName(serviceName).newInstance();
            attachMethod.invoke(targetService, this, activityThread, intent.getComponent().getClassName(),             token,
                getApplication(), originAMN);
            //service的oncreate方法
            Method onCreateMethod = serviceClz.getDeclaredMethod("onCreate");
            onCreateMethod.setAccessible(true);
            onCreateMethod.invoke(targetService);
        targetService.onStartCommand(intent, flags, startId);
        } catch (Exception e) {
            e.printStackTrace();
            Log.e(TAG, "onStartCommand: " + e.getMessage());
        }
    }
    return super.onStartCommand(intent, flags, startId);
}
```

代码较长，逻辑如下：

* 检测到需要启动插件Service
* 构建插件Service attach方法需要的参数
* 构造一个插件Service
* 调用插件Service的attach方法
* 调用插件Service的onCreate方法

这样，一个插件Service就启动起来了。

### BroadcastReceiver的插件化

BroadcastReceiver分为两种，静态注册，和动态注册。静态注册的是PMS在安装或者系统启动的时候扫描APK，解析配置文件，并存储在PMS端的，这个我们无法干预，并且，我们的插件由于未安装，静态注册的是无法通过系统正常行为装载的。而动态注册的，由于没有检测这一步，因此，也不需要我们干预。我们现在需要解决的问题就是，怎么能装载插件中静态注册的。

我们可以通过解析配置文件，自己调用动态注册的方法去注册这个。

代码这里就不贴了，和下面ContentProvider的一起贴。


### ContentProvider的插件化

和其他三个组件不一样的是，ContentProvider是在进程启动入口，也就是ActivityThread中进行安装的。那么我们可以按照这个思路，自己去进行安装的操作。

代码如下。

```
Field providersField = packageClz.getDeclaredField("providers");
providersField.setAccessible(true);
ArrayList providers = (ArrayList) providersField.get(packageObject);

Class providerClz = Class.forName("android.content.pm.PackageParser$Provider");
Field providerInfoField = providerClz.getDeclaredField("info");
providersField.setAccessible(true);
List<ProviderInfo> providerInfos = new ArrayList<>();
for (int i = 0; i < providers.size(); i++) {
    ProviderInfo providerInfo = (ProviderInfo) providerInfoField.get(providers.get(i));
    providerInfo.applicationInfo = getApplicationInfo();
    providerInfos.add(providerInfo);
}
Class contextImplClz = Class.forName("android.app.ContextImpl");
Field mMainThread = contextImplClz.getDeclaredField("mMainThread");
mMainThread.setAccessible(true);
Object activityThread = mMainThread.get(this.getBaseContext());
Class activityThreadClz = Class.forName("android.app.ActivityThread");
Method installContentProvidersMethod = activityThreadClz.getDeclaredMethod("installContentProviders",
    Context.class, List.class);
installContentProvidersMethod.setAccessible(true);
installContentProvidersMethod.invoke(activityThread, this, providerInfos);
```


贴一下整体的代码,这里的代码，包括Multidex方法加dex，BroadcastReceiver的插件化以及ContentProvider的插件化。

```
private void loadClassByHostClassLoader() {
    File apkFile = new File("/sdcard/plugin_1.apk");
    ClassLoader baseClassLoader = this.getClassLoader();
    try {
        Field pathListField = baseClassLoader.getClass().getSuperclass().getDeclaredField("pathList");
        pathListField.setAccessible(true);
        Object pathList = pathListField.get(baseClassLoader);

        Class clz = Class.forName("dalvik.system.DexPathList");
        Field dexElementsField = clz.getDeclaredField("dexElements");
        dexElementsField.setAccessible(true);
        Object[] dexElements = (Object[]) dexElementsField.get(pathList);

        Class elementClz = dexElements.getClass().getComponentType();
        Object[] newDexElements = (Object[]) Array.newInstance(elementClz, dexElements.length + 1);
        Constructor<?> constructor = elementClz.getConstructor(File.class, boolean.class, File.class,  
            DexFile.class);
            File file = new File(getFilesDir(), "test.dex");
        if (file.exists()) {
            file.delete();
        }
        file.createNewFile();
        Object pluginElement = constructor.newInstance(apkFile, false, apkFile, 
            DexFile.loadDex(apkFile.getCanonicalPath(),
            file.getAbsolutePath(), 0));
        Object[] toAddElementArray = new Object[]{pluginElement};
        System.arraycopy(dexElements, 0, newDexElements, 0, dexElements.length);
        // 插件的那个element复制进去
        System.arraycopy(toAddElementArray, 0, newDexElements, dexElements.length, toAddElementArray.length);
        dexElementsField.set(pathList, newDexElements);

        AssetManager assetManager = getResources().getAssets();
        Method method = assetManager.getClass().getDeclaredMethod("addAssetPath", String.class);
        method.invoke(assetManager, apkFile.getPath());

        //            PackageInfo packageInfo = getPackageManager().getPackageArchiveInfo(apkFile.getAbsolutePath(), PackageManager.GET_RECEIVERS);
        //            if (packageInfo != null) {
        //                for (ActivityInfo info : packageInfo.receivers) {
        //                    Log.e(TAG, "loadClassByHostClassLoader: " + info.name );
        //
        //                }
        //            }
        Class packageParseClz = Class.forName("android.content.pm.PackageParser");
        Object packageParser = packageParseClz.newInstance();
        Method parseMethod = packageParseClz.getDeclaredMethod("parsePackage", File.class, int.class);
        parseMethod.setAccessible(true);
        Object packageObject = parseMethod.invoke(packageParser, apkFile, 1 << 2);
        Class packageClz = Class.forName("android.content.pm.PackageParser$Package");
        Field receiversField = packageClz.getDeclaredField("receivers");
        receiversField.setAccessible(true);
        ArrayList receives = (ArrayList) receiversField.get(packageObject);

        Class componentClz = Class.forName("android.content.pm.PackageParser$Component");
        Field intents = componentClz.getDeclaredField("intents");
        intents.setAccessible(true);
        Field classNameField = componentClz.getDeclaredField("className");
        classNameField.setAccessible(true);
        for (int i = 0; i < receives.size(); i++) {
            ArrayList<IntentFilter> intentFilters = (ArrayList<IntentFilter>) intents.get(receives.get(i));
            String className = (String) classNameField.get(receives.get(i));
            registerReceiver((BroadcastReceiver) getClassLoader().loadClass(className).newInstance(),
                intentFilters.get(0));
        }

        // 安装ContentProvider
        Field providersField = packageClz.getDeclaredField("providers");
        providersField.setAccessible(true);
        ArrayList providers = (ArrayList) providersField.get(packageObject);

        Class providerClz = Class.forName("android.content.pm.PackageParser$Provider");
        Field providerInfoField = providerClz.getDeclaredField("info");
        providersField.setAccessible(true);
        List<ProviderInfo> providerInfos = new ArrayList<>();
        for (int i = 0; i < providers.size(); i++) {
            ProviderInfo providerInfo = (ProviderInfo) providerInfoField.get(providers.get(i));
            providerInfo.applicationInfo = getApplicationInfo();
            providerInfos.add(providerInfo);
        }
        Class contextImplClz = Class.forName("android.app.ContextImpl");
        Field mMainThread = contextImplClz.getDeclaredField("mMainThread");
        mMainThread.setAccessible(true);
        Object activityThread = mMainThread.get(this.getBaseContext());
        Class activityThreadClz = Class.forName("android.app.ActivityThread");
        Method installContentProvidersMethod = activityThreadClz.getDeclaredMethod("installContentProviders",
            Context.class, List.class);
        installContentProvidersMethod.setAccessible(true);
        installContentProvidersMethod.invoke(activityThread, this, providerInfos);
    } catch (Exception e) {
        e.printStackTrace();
        Log.e(TAG, "loadClassByHostClassLoader: " + e.getMessage());
    }
}
```



>到这里，四大组件的插件化方案介绍了一点点，虽然每种组件只介绍了一种方法。上面的内容忽略了大部分源码细节。这部分内容需要大家自己去补。


### 资源的插件化方案

资源的插件化方案，目前有两种

* 合并资源方案
* 各个插件构造自己的资源方案

今天，我们介绍第一种方案，合并资源方案，合并资源方案，我们只需要往现有的AssetManager中调用addAsset添加一个资源即可，当然，存在比较多适配问题，我们暂时忽略。合并资源方案最大的问题就是资源冲突。要解决资源冲突，有两种办法。

* 修改AAPT，能自由修改PP段
* 干预编译过程，修改ASRC和R文件


为了简单演示，我直接只用VirtualApk的编译插件去做。实际上VirtualApk的编译插件来自以Small的编译插件。只要对文件格式熟悉，这个还是很好写的。


```
AssetManager assetManager = getResources().getAssets();
Method method = assetManager.getClass().getDeclaredMethod("addAssetPath", String.class);
method.invoke(assetManager, apkFile.getPath());
```

我们只需要上面简单的代码，就能完成资源的插件化。当然，这里忽略了版本差异。


### SO的插件化方案

so的插件化方案，我这里介绍修改dexpathlist的方案。我们要做的是什么呢？只需要往nativeLibraryPathElements中添加SO的Element，并且往nativeLibraryDirectories添加so路径就可以了。
代码如下。


```
Method findLibMethod = elementClz.getDeclaredMethod("findNativeLibrary",String.class);
findLibMethod.setAccessible(true);
//            Object soElement = constructor.newInstance(new File("/sdcard/"), true, apkFile, DexFile.loadDex(apkFile.getCanonicalPath(),
//                    file.getAbsolutePath(), 0));
//            findLibMethod.invoke(pluginElement,System.mapLibraryName("native-lib"));
ZipFile zipFile = new ZipFile(apkFile);
ZipEntry zipEntry = zipFile.getEntry("lib/armeabi/libnative-lib.so");
InputStream inputStream = zipFile.getInputStream(zipEntry);
File outSoFile = new File(getFilesDir(), "libnative-lib.so");
if (outSoFile.exists()) {
    outSoFile.delete();
}
FileOutputStream outputStream = new FileOutputStream(outSoFile);
byte[] cache = new byte[2048];
int count = 0;
while ((count = inputStream.read(cache)) != -1) {
    outputStream.write(cache, 0, count);
}
outputStream.flush();
outputStream.close();
inputStream.close();
// 构造Element
Object soElement = constructor.newInstance(getFilesDir(), true, null, null);
//            findLibMethod.invoke(soElement,System.mapLibraryName("native-lib"));

// 将soElement填充到nativeLibraryPathElements中,
Field soElementField = clz.getDeclaredField("nativeLibraryPathElements");
soElementField.setAccessible(true);
Object[] soElements = (Object[]) soElementField.get(pathList);
Object[] newSoElements = (Object[]) Array.newInstance(elementClz, soElements.length + 1);
Object[] toAddSoElementArray = new Object[]{soElement};
System.arraycopy(soElements, 0, newSoElements, 0, soElements.length);
// 插件的那个element复制进去
System.arraycopy(toAddSoElementArray, 0, newSoElements, soElements.length, toAddSoElementArray.length);
soElementField.set(pathList, newSoElements);

//将so的文件夹填充到nativeLibraryDirectories中
Field libDir = clz.getDeclaredField("nativeLibraryDirectories");
libDir.setAccessible(true);
List libDirs = (List) libDir.get(pathList);
libDirs.add(getFilesDir());
libDir.set(pathList,libDirs);
```


### 总结

在前人的精心研究下，插件化方案已经很成熟了。插件化方案的难点主要在适配方面。其他倒还好。



