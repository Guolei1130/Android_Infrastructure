* 仓库地址：https://github.com/didi/booster
* 分析版本：master分支 commit 079f278ed6ae2da4b85cf2e33d1b01c5d02e1107

## BoosterPlugin插件
我们先看一下这个插件代码。

```kotlin
    override fun apply(project: Project) {
        when {
            project.plugins.hasPlugin("com.android.application") -> project.getAndroid<AppExtension>().let { android ->
                android.registerTransform(BoosterAppTransform())
                project.afterEvaluate {
                    ServiceLoader.load(VariantProcessor::class.java, javaClass.classLoader).toList().let { processors ->
                        android.applicationVariants.forEach { variant ->
                            processors.forEach { processor ->
                                processor.process(variant)
                            }
                        }
                    }
                }
            }
            project.plugins.hasPlugin("com.android.library") -> project.getAndroid<LibraryExtension>().let { android ->
                android.registerTransform(BoosterLibTransform())
                project.afterEvaluate {
                    ServiceLoader.load(VariantProcessor::class.java, javaClass.classLoader).toList().let { processors ->
                        android.libraryVariants.forEach { variant ->
                            processors.forEach { processor ->
                                processor.process(variant)
                            }
                        }
                    }
                }
            }
        }
    }
```

从代码上来看，我们可以很清晰的知道。Booster分为两块，分别是transform以及VariantProcessor。通过ServiceLoader加载各种VariantProcessor 并依次处理。

## VariantProcessor
那么，在Booster中，都有哪些VariantProcessor呢,在booster-task-all/build.gradle文件中，有如下依赖。

```groovy
dependencies {
    compile project(':booster-task-artifact')
    compile project(':booster-task-compression')
    compile project(':booster-task-dependency')
    compile project(':booster-task-permission')
}

```

接下来，我们挨个分析。

```kotlin
    override fun process(variant: BaseVariant) {
        val tasks = variant.scope.globalScope.project.tasks
        val artifacts = tasks.findByName("showArtifacts") ?: tasks.create("showArtifacts")
        tasks.create("show${variant.name.capitalize()}Artifacts", ArtifactsResolver::class.java) {
            it.variant = variant
            it.outputs.upToDateWhen { false }
        }.also {
            artifacts.dependsOn(it)
        }
    }
```

各个VariantProcessor的代码结构一直，基本上是创建task，所以，后面我们直接看task的内容(除非一些特殊情况)

#### Artifact

```
   @TaskAction
    fun run() {
        val artifacts = this.variant.scope.allArtifacts
        val maxTypeWidth: Int = artifacts.keys.map { it.length }.max()!!

        artifacts.forEach { type, files ->
            println("${".".repeat(maxTypeWidth - type.length + 1)}$type : $files")
        }
    }

```

可以看到，就是输出artifacts内容而已，似乎没什么实质性的用处。

#### compression
```kotlin
variant.processResTask.doLast {
            variant.compressProcessedRes(results)
            variant.generateReport(results)
        }
```
首先，在processResTask任务之后，压缩一下res并且声称报告。

```kotlin
private fun BaseVariant.compressProcessedRes(results: CompressionResults) {
    val files = scope.processedRes.search {
        it.name.startsWith(SdkConstants.FN_RES_BASE) && it.extension == SdkConstants.EXT_RES
    }
    files.parallelStream().forEach { ap_ ->
        val s0 = ap_.length()
        ap_.repack {
            !NO_COMPRESS.contains(it.name.substringAfterLast('.'))
        }
        val s1 = ap_.length()
        results.add(CompressionResult(ap_, s0, s1, ap_))
    }
}

private fun File.repack(shouldCompress: (ZipEntry) -> Boolean) {
    val dest = File.createTempFile(SdkConstants.FN_RES_BASE + SdkConstants.RES_QUALIFIER_SEP, SdkConstants.DOT_RES)

    ZipOutputStream(dest.outputStream()).use { output ->
        ZipFile(this).use { zip ->
            zip.entries().asSequence().forEach { origin ->
                val target = ZipEntry(origin.name).apply {
                    size = origin.size
                    crc = origin.crc
                    comment = origin.comment
                    extra = origin.extra
                    method = if (shouldCompress(origin)) ZipEntry.DEFLATED else origin.method
                }

                output.putNextEntry(target)
                zip.getInputStream(origin).use {
                    it.copyTo(output)
                }
                output.closeEntry()
            }
        }
    }

    if (this.delete()) {
        if (!dest.renameTo(this)) {
            dest.copyTo(this, true)
        }
    }
}

```

这里压缩的原理就是改变一下压缩方式。
再然后是去掉冗余资源，压缩png和assets资源。

```kotlin
        val klassRemoveRedundantFlatImages = if (aapt2) RemoveRedundantFlatImages::class else RemoveRedundantImages::class
        val reduceRedundancy = variant.project.tasks.create("remove${variant.name.capitalize()}RedundantResources", klassRemoveRedundantFlatImages.java) {
            it.outputs.upToDateWhen { false }
            it.variant = variant
            it.results = results
            it.sources = { variant.scope.mergedRes.search(pngFilter) }
        }.dependsOn(variant.mergeResourcesTask)

        variant.project.compressor?.apply {
            newCompressionTaskCreator().apply {
                createAssetsCompressionTask(variant, results)
                createResourcesCompressionTask(variant, results).dependsOn(reduceRedundancy)
            }
        }
```

如何查找出冗余资源呢,很搞笑的是，booster中，以同名资源作为冗余资源，会根据density排序，然后删除掉一份，加入xhdpi和xxhdpi都有，则会保留个xxhdpi，这和我们所理解的冗余似乎有差别。代码如下

```kotlin
        val resources = sources().parallelStream().map {
            it to it.metadata
        }.collect(Collectors.toSet())

        resources.filter {
            it.second != null
        }.groupBy({
            it.second!!.resourceName.substringBeforeLast('/')
        }, {
            it.first to it.second
        }).forEach { entry ->
            entry.value.groupBy({
                it.second!!.resourceName.substringAfterLast('/') //按名字分组
            }, {
                it.first to it.second!!
            }).map { group ->
                group.value.sortedByDescending {
                    it.second.config.density // 排序
                }.takeLast(group.value.size - 1) //取出一个
            }.flatten().parallelStream().forEach {
                try {
                    if (it.first.delete()) { //删掉
                        val original = File(it.second.sourcePath)
                        results.add(CompressionResult(it.first, original.length(), 0, original))
                    } else {
                        logger.error("Cannot delete file `${it.first}`")
                    }
                } catch (e: IOException) {
                    logger.error("Cannot delete file `${it.first}`", e)
                }
            }
        }
```

那如何压缩资源呢。booster中 根据不同的配置和sdk 选择不同的工具压缩。代码如下

```kotlin
        fun get(project: Project): CompressionTool? {
            val pngquant = project.findProperty(PROPERTY_PNGQUANT)?.toString()
            val compressor = project.findProperty(PROPERTY_COMPRESSOR)?.toString()
            val binDir = project.buildDir.file(SdkConstants.FD_OUTPUT).absolutePath
            val minSdkVersion = project.getAndroid<BaseExtension>().defaultConfig.minSdkVersion.apiLevel

            project.logger.info("minSdkVersion: $minSdkVersion$")
            project.logger.info("$PROPERTY_COMPRESSOR: $compressor")
            project.logger.info("$PROPERTY_PNGQUANT: $pngquant")

            return when (compressor) {
                Cwebp.PROGRAM -> Cwebp(binDir)
                Pngquant.PROGRAM -> Pngquant(pngquant)
                else -> when {
                    minSdkVersion >= 18 -> Cwebp(binDir)
                    minSdkVersion in 15..17 -> Cwebp(binDir, true)
                    else -> Pngquant(pngquant).let {
                        if (it.isInstalled) it else null
                    }
                }
            }
        }
```

选择好工具之后，就执行相应工具的压缩命令压缩即可。如下

```kotlin
        sources().map {
            ActionData(it, it, listOf(cmdline.executable!!.absolutePath, "--strip", "--skip-if-larger", "-f", "--ext", DOT_PNG, "-s", "1", it.absolutePath))
        }.parallelStream().forEach {
            val s0 = it.input.length()
            val rc = project.exec { spec ->
                spec.isIgnoreExitValue = true
                spec.commandLine = it.cmdline
            }
            when (rc.exitValue) {
                0 -> {
                    val s1 = it.input.length()
                    results.add(CompressionResult(it.input, s0, s1, it.input))
                }
                else -> {
                    logger.error("${CSI_RED}Command `${it.cmdline.joinToString(" ")}` exited with non-zero value ${rc.exitValue}$CSI_RESET")
                    results.add(CompressionResult(it.input, s0, s0, it.input))
                }
            }
        }
```

#### dependency

```
    @TaskAction
    fun run() {
        if (!variant.buildType.isDebuggable) {
            variant.dependencies.filter {
                it.id.componentIdentifier is MavenUniqueSnapshotComponentIdentifier
            }.map {
                it.id.componentIdentifier as MavenUniqueSnapshotComponentIdentifier
            }.ifNotEmpty { snapshots ->
                println("$CSI_YELLOW ⚠️  ${snapshots.size} SNAPSHOT artifacts found in ${variant.name} variant:$CSI_RESET\n${snapshots.joinToString("\n") { snapshot -> "$CSI_YELLOW→  ${snapshot.displayName}$CSI_RESET" }}")
            }
        }
    }
```

这个task就是查找出所有SNAPSHOT类型的依赖，并以黄色字体输出警告。

#### persmission 

``` kotlin
    @TaskAction
    fun run() {
        variant.scope.getArtifactFileCollection(RUNTIME_CLASSPATH, ALL, AAR).files.forEach { aar ->
            ZipFile(aar).use { zip ->
                zip.getEntry(SdkConstants.FN_ANDROID_MANIFEST_XML)?.let { entry ->
                    zip.getInputStream(entry).use { source ->
                        PermissionUsageHandler().also { handler ->
                            factory.newSAXParser().parse(source, handler)
                        }.permissions.sorted().ifNotEmpty { permissions ->
                            println("${aar.componentId} [$$CSI_YELLOW${variant.name}$CSI_RESET]")
                            permissions.forEach { permission ->
                                println("  - $permission")
                            }
                        }
                    }
                }
            }
        }
    }
```

通过解析AndroidManidfest.xml文件，去输出里面包含的权限。



## BoosterTransform


``` kotlin
BoosterTransformInvocation(invocation).apply {
            dumpInputs(this)

            if (isIncremental) {
                onPreTransform(this)
                doIncrementalTransform()
            } else {
                val dexBuilder = File(
                    listOf(
                        buildDir.absolutePath,
                        AndroidProject.FD_INTERMEDIATES,
                        "transforms",
                        "dexBuilder"
                    ).joinToString(File.separator)
                )
                if (dexBuilder.exists()) {
                    dexBuilder.deleteRecursively()
                }
                outputProvider.deleteAll()
                onPreTransform(this)
                doFullTransform()
            }

            this.onPostTransform(this)
        }.executor.apply {
            shutdown()
            awaitTermination(1, TimeUnit.MINUTES)
        }
```

在transform方法中，将操作转交给BoosterTransformInvocation来做。

* onPreTransform
* doIncrementalTransform
* doFullTransform 

在BoosterTransformInvocation中，首先会通过ServiceLoader拿到所有的Transformer(滴滴包).

``` kotlin
private val transformers = ServiceLoader.load(Transformer::class.java, javaClass.classLoader).toList()
```

接下来，以doFullTransform为例

```kotlin
    internal fun doFullTransform() {
        this.inputs.parallelStream().forEach { input ->
            input.directoryInputs.parallelStream().forEach {
                project.logger.info("Transforming ${it.file}")
                it.file.transform(outputProvider.getContentLocation(it.file.name, it.contentTypes, it.scopes, Format.DIRECTORY)) { bytecode ->
                    bytecode.transform(this)
                }
            }
            input.jarInputs.parallelStream().forEach {
                project.logger.info("Transforming ${it.file}")
                it.file.transform(outputProvider.getContentLocation(it.name, it.contentTypes, it.scopes, Format.JAR)) { bytecode ->
                    bytecode.transform(this)
                }
            }
        }
    }
```
会拿到所有的文件的ByteArray，然后transform，ByteArray的transform是一个扩展方法

```kotlin
    private fun ByteArray.transform(invocation: BoosterTransformInvocation): ByteArray {
        return transformers.fold(this) { bytes, transformer ->
            transformer.transform(invocation, bytes)
        }
    }
```

在这个方法中，会依次使用Transformer来处理bytes。

那么，trnasform有哪些呢，只有一个，就是AsmTransformer。
```kotlin
    private val transformers = ServiceLoader.load(ClassTransformer::class.java, javaClass.classLoader).toList()

    override fun transform(context: TransformContext, bytecode: ByteArray): ByteArray {
        return ClassWriter(ClassWriter.COMPUTE_MAXS).also { writer ->
            transformers.fold(ClassNode().also { klass ->
                ClassReader(bytecode).accept(klass, 0)
            }) { klass, transformer ->
                transformer.transform(context, klass)
            }.accept(writer)
        }.toByteArray()
    }

    override fun onPreTransform(context: TransformContext) {
        transformers.forEach {
            it.onPreTransform(context)
        }
    }

    override fun onPostTransform(context: TransformContext) {
        transformers.forEach {
            it.onPostTransform(context)
        }
    }
```

在这个里面，会加载到所有的ClassTransformer并依次处理字节码。

ClassTransformer有如下几种。

```groovy
dependencies {
    compile project(':booster-transform-lint')
    compile project(':booster-transform-logcat')
    compile project(':booster-transform-activity-thread')
    compile project(':booster-transform-finalizer-watchdog-daemon')
    compile project(':booster-transform-media-player')
    compile project(':booster-transform-res-check')
    compile project(':booster-transform-shared-preferences')
    compile project(':booster-transform-shrink')
    compile project(':booster-transform-thread')
    compile project(':booster-transform-toast')
    compile project(':booster-transform-usage')
    compile project(':booster-transform-webview')
}

```

#### lint
生成dot，分析调用关系，略过~
#### logcat
```kotlin
klass.methods.forEach { method ->
            method.instructions?.iterator()?.asIterable()?.filter {
                when (it.opcode) {
                    INVOKESTATIC -> (it as MethodInsnNode).owner == LOGCAT && SHADOW_LOG_METHODS.contains(it.name)
                    INVOKEVIRTUAL -> (it as MethodInsnNode).name == "printStackTrace" && it.desc == "()V" && context.klassPool.get(THROWABLE).isAssignableFrom(it.owner)
                    GETSTATIC -> (it as FieldInsnNode).owner == SYSTEM && (it.name == "out" || it.name == "err")
                    else -> false
                }
            }?.forEach {
                when (it.opcode) {
                    INVOKESTATIC -> {
                        logger.println(" * ${(it as MethodInsnNode).owner}.${it.name}${it.desc} => $SHADOW_LOG.${it.name}${it.desc}: ${klass.name}.${method.name}${method.desc}")
                        it.owner = SHADOW_LOG
                    }
                    INVOKEVIRTUAL -> {
                        logger.println(" * ${(it as MethodInsnNode).owner}.${it.name}${it.desc} => $SHADOW_LOG.${it.name}${it.desc}: ${klass.name}.${method.name}${method.desc}")
                        it.apply {
                            itf = false
                            owner = SHADOW_THROWABLE
                            desc = "(Ljava/lang/Throwable;)V"
                            opcode = INVOKESTATIC
                        }
                    }
                    GETSTATIC -> {
                        logger.println(" * ${(it as FieldInsnNode).owner}.${it.name}${it.desc} => $SHADOW_LOG.${it.name}${it.desc}: ${klass.name}.${method.name}${method.desc}")
                        it.owner = SHADOW_SYSTEM
                    }
                }
            }
        }
```

查找到对应的方法调用，并替换为SHADOW,**由于几乎所有的代码逻辑都是这样，因此，后面就简单的描述下功能了。**

```kotlin
private const val LOGCAT = "android/util/Log"
private const val THROWABLE = "java/lang/Throwable"
private const val SYSTEM = "java/lang/System"
private const val INSTRUMENT = "com/didiglobal/booster/instrument/"
private const val SHADOW_LOG = "${INSTRUMENT}ShadowLog"
private const val SHADOW_SYSTEM = "${INSTRUMENT}ShadowSystem"
private const val SHADOW_THROWABLE = "${INSTRUMENT}ShadowThrowable"
private val SHADOW_LOG_METHODS = setOf("v", "d", "i", "w", "e", "wtf", "println")
```
#### activity-thread
```kotlin
    override fun transform(context: TransformContext, klass: ClassNode): ClassNode {
        if (!this.applications.contains(klass.className)) {
            return klass
        }

        mapOf(
            "<clinit>()V" to klass.defaultClinit,
            "<init>()V" to klass.defaultInit,
            "onCreate()V" to klass.defaultOnCreate
        ).forEach { (unique, defaultMethod) ->
            val method = klass.methods?.find {
                "${it.name}${it.desc}" == unique
            } ?: defaultMethod
            method.instructions?.findAll(RETURN, ATHROW)?.forEach {
                method.instructions?.insertBefore(it, MethodInsnNode(INVOKESTATIC, ACTIVITY_THREAD_HOOKER, "hook", "()V", false))
                logger.println(" + $ACTIVITY_THREAD_HOOKER.hook()V before @${if (it.opcode == ATHROW) "athrow" else "return"}: ${klass.name}.${method.name}${method.desc}")
            }
        }

        return klass
    }
```
在Application的几个最初的方法进行hook，防止出现应用启动崩溃的情况。

```kotlin
   @Override
    public final boolean handleMessage(final Message msg) {
        try {
            this.mHandler.handleMessage(msg);
        } catch (final NullPointerException e) {
            if (hasStackTraceElement(e, ASSET_MANAGER_GET_RESOURCE_VALUE, LOADED_APK_GET_ASSETS)) {
                abort(e);
            }
            rethrowIfNotCausedBySystem(e);
        } catch (final SecurityException
                | IllegalArgumentException
                | AndroidRuntimeException
                | WindowManager.BadTokenException e) {
            rethrowIfNotCausedBySystem(e);
        } catch (final Resources.NotFoundException e) {
            rethrowIfNotCausedBySystem(e);
            abort(e);
        } catch (final RuntimeException e) {
            final Throwable cause = e.getCause();
            if (((Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) && isCausedBy(cause, DeadSystemException.class))
                    || (isCausedBy(cause, NullPointerException.class) && hasStackTraceElement(e, LOADED_APK_GET_ASSETS))) {
                abort(e);
            }
            rethrowIfNotCausedBySystem(e);
        } catch (final Error e) {
            rethrowIfNotCausedBySystem(e);
            abort(e);
        }

        return true;
    }
```
#### finalizer-watchdog-daemon
``` kotlin
    override fun transform(context: TransformContext, klass: ClassNode): ClassNode {
        if (!this.applications.contains(klass.className)) {
            return klass
        }

        val method = klass.methods?.find {
            "${it.name}${it.desc}" == "attachBaseContext(Landroid/content/Context;)V"
        } ?: klass.defaultAttachBaseContext

        method.instructions?.findAll(RETURN, ATHROW)?.forEach {
                method.instructions?.insertBefore(it, MethodInsnNode(INVOKESTATIC, FINALIZER_WATCHDOG_DAEMON_KILLER, "kill", "()V", false))
                logger.println(" + $FINALIZER_WATCHDOG_DAEMON_KILLER.kill()V before @${if (it.opcode == ATHROW) "athrow" else "return"}: ${klass.name}.${method.name}${method.desc} ")
        }

        return klass
    }
```

在Application attachbase方法中，把watchdog干掉，防止finilizer方法超时的bug，这个bug还是挺常见的。
``` kotlin
final Class clazz = Class.forName("java.lang.Daemons$FinalizerWatchdogDaemon");
                        final Field field = clazz.getDeclaredField("INSTANCE");
                        field.setAccessible(true);
                        final Object watchdog = field.get(null);

                        try {
                            final Field thread = clazz.getSuperclass().getDeclaredField("thread");
                            thread.setAccessible(true);
                            thread.set(watchdog, null);
                        } catch (final Throwable t) {
                            Log.e(TAG, "Clearing reference of thread `FinalizerWatchdogDaemon` failed", t);

                            try {
                                final Method method = clazz.getSuperclass().getDeclaredMethod("stop");
                                method.setAccessible(true);
                                method.invoke(watchdog);
                            } catch (final Throwable e) {
                                Log.e(TAG, "Interrupting thread `FinalizerWatchdogDaemon` failed", e);
                                break;
                            }
                        }
```
非常粗暴，直接停掉
#### media-player
```kotlin
    override fun transform(context: TransformContext, klass: ClassNode): ClassNode {
        if (klass.name == SHADOW_MEDIA_PLAYER) {
            return klass
        }
        klass.methods?.forEach { method ->
            method.instructions?.iterator()?.asIterable()?.filter {
                when (it.opcode) {
                    Opcodes.INVOKESTATIC -> (it as MethodInsnNode).owner == MEDIA_PLAYER && it.name == "create"
                    Opcodes.NEW -> (it as TypeInsnNode).desc == MEDIA_PLAYER
                    else -> false
                }
            }?.forEach {
                if (it.opcode == Opcodes.INVOKESTATIC) {
                    logger.println(" * ${(it as MethodInsnNode).owner}.${it.name}${it.desc} => $SHADOW_MEDIA_PLAYER.${it.name}${it.desc}: ${klass.name}.${method.name}${method.desc}")
                    it.owner = SHADOW_MEDIA_PLAYER
                } else if (it.opcode == Opcodes.NEW) {
                    (it as TypeInsnNode).transform(klass, method, it, SHADOW_MEDIA_PLAYER)
                    logger.println(" * new ${it.desc}() => $SHADOW_MEDIA_PLAYER.newMediaPlayer:()L$MEDIA_PLAYER: ${klass.name}.${method.name}${method.desc}")
                }
            }
        }
        return klass
    }
```

替换掉create相关的方法，用ShadowMediaPlayer的静态方法去包装一下，hook掉mCallback，try catch，强行catch异常，不崩溃。
#### res-check
```kotlin
    public static void checkRes(final Application app) {
        if (null == app.getAssets() || null == app.getResources()) {
            final int pid = Process.myPid();
            Log.w(TAG, "Process " + pid + " is going to be killed");
            Process.killProcess(pid);
            System.exit(10);
        }
    }
```

不懂这个意义何在。
#### shared-preferences
sp在主线程操作可能引起卡顿或者ANR，因此，放入子线程进行操作。
```kotlin
    public static void apply(final SharedPreferences.Editor editor) {
        if (Looper.myLooper() == Looper.getMainLooper()) {
            AsyncTask.SERIAL_EXECUTOR.execute(new Runnable() {
                @Override
                public void run() {
                    editor.commit();
                }
            });
        } else {
            editor.commit();
        }
    }
```
#### shrink
常量折叠，删除无用的R文件等等。
#### thread
这里就是将thread相关的代码，替换成dd的那个代码，起个有效的名字等等，线程池等等。不说了。
#### toast
解决BadToken的问题，通过hook callback，catch异常。
#### usage
编译时api lint警告，可以用于一些过时api，或者受限api lint
```kotlin
    override fun transform(context: TransformContext, klass: ClassNode): ClassNode {
        if (context.hasProperty(PROPERTY_USED_APIS)) {
            val apis = context.usedApis

            klass.methods.forEach { method ->
                method.instructions.iterator().asSequence().filterIsInstance(MethodInsnNode::class.java).map {
                    "${it.owner}.${it.name}${it.desc}"
                }.filter {
                    apis.contains(it)
                }.forEach {
                    println("$CSI_YELLOW ! ${klass.name}.${method.name}${method.desc}: $CSI_RESET$it")
                }
            }
        }

        return klass
    }
```
#### webview

通过字节码插入的方式，在主线程idle时，加载Webview依赖的provider。

```kotlin
    private static void startChromiumEngine() {
        try {
            final long t0 = SystemClock.uptimeMillis();
            final Object provider = invokeStaticMethod(Class.forName("android.webkit.WebViewFactory"), "getProvider");
            invokeMethod(provider, "startYourEngines", new Class[]{boolean.class}, new Object[]{true});
            Log.i(TAG, "Start chromium engine complete: " + (SystemClock.uptimeMillis() - t0) + " ms");
        } catch (final Throwable t) {
            Log.e(TAG, "Start chromium engine error", t);
        }
    }
```

## 总结
这个框架整体设计还是很优秀的，灵活性、可扩展性非常高，原理也比较简单，不复杂，有些功能还是很实用的。缺点就是有些模块处理的相对粗糙一点，由于编译时插入了字节码，会影响堆栈信息，因此可能对影响线上问题的排查。