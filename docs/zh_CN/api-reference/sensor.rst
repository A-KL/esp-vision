sensor -- 摄像头
================

:link_to_translation:`en:[English]`

``sensor`` 模块用于控制摄像头并采集图像帧。它的接口与 OpenMV 的 ``sensor`` 保持一致，因此大多数 OpenMV 脚本无需改动即可移植。

摄像头初始化与连续采集
----------------------

典型流程是复位传感器、选择像素格式与分辨率、等待自动曝光和自动白平衡稳定，然后连续采集图像：

.. code-block:: python

   import sensor

   sensor.reset()
   sensor.set_pixformat(sensor.RGB565)
   sensor.set_framesize(sensor.QVGA)
   sensor.skip_frames(time=2000)

   while True:
       img = sensor.snapshot()

``sensor.reset()`` 会应用开发板的默认摄像头配置。``RGB565`` 适用于显示、绘图和大多数 AI 工作流，``GRAYSCALE`` 则可降低许多检测算法的内存占用和处理开销。``sensor.snapshot()`` 返回由可复用帧缓冲承载的 :py:class:`image.Image`，因此需要在下一次采集后继续保持图像内容不变时，应先复制该图像。

图像方向与摄像头状态
--------------------

摄像头传感器的实际安装方向可能导致画面翻转，可通过水平镜像和垂直翻转进行校正。``status()`` 返回当前分辨率、像素格式、传感器 ID、图像方向和裁剪信息，可用于诊断：

.. code-block:: python

   import sensor

   sensor.reset()
   sensor.set_hmirror(True)
   sensor.set_vflip(False)

   info = sensor.status()
   print("sensor id:", info["id"])
   print("output:", info["width"], "x", info["height"])
   print("ready:", info["ready"])

镜像和翻转设置会影响后续采集的图像。产品代码可在初始化后调用 ``status()``，确认摄像头已经就绪，并验证协商得到的输出尺寸与处理链路一致。

暂时停止采集
------------

不需要采集时可以停止摄像头流，并在下一次采集前重新启动：

.. code-block:: python

   sensor.shutdown(True)
   # 执行不需要摄像头的任务。
   sensor.shutdown(False)
   sensor.skip_frames(n=3)
   img = sensor.snapshot()

重新启动摄像头流后，如果曝光或输入帧队列需要重新稳定，应丢弃少量图像帧。

运行时控制 IPA
--------------

IPA 和 ISP 控制接口是可选固件功能。使用前可通过 ``hasattr(sensor, "get_ipa")`` 或 ``hasattr(sensor, "get_isp")`` 检查；固件未包含该功能时，这些方法不存在。

使用 ``sensor.set_ipa(False)`` 停止图像处理算法（IPA）控制器，使用 ``sensor.set_ipa(True)`` 重新启动。调用前需要初始化摄像头。``sensor.get_ipa()`` 返回控制器的当前状态；不支持运行时控制时返回 ``None``。对不支持的摄像头设置 IPA 会抛出 ``OSError``。

.. code-block:: python

   sensor.reset()
   sensor.skip_frames(time=2000)
   if hasattr(sensor, "get_ipa") and sensor.get_ipa() is not None:
       sensor.set_ipa(False)
       print("IPA:", sensor.get_ipa())
       img = sensor.snapshot()
       img.flush()
       sensor.set_ipa(True)

切换 IPA 状态会短暂停止并重新启动采集，保留输出像素格式、分辨率、镜像和翻转设置。重复设置当前状态不会执行切换。``sensor.reset()`` 以及 ``sensor.shutdown(True)`` 后重新启动摄像头会恢复默认 IPA 状态。

切换失败时摄像头不会退回原状态，而是被完全关闭：抛出 ``OSError`` 之前，采集缓冲、缩放器和摄像头设备都已释放。此时需调用 ``sensor.reset()`` 重新启动摄像头。

关闭 IPA 会停止其自动参数更新，但不会绕过 ISP 或清除已有调校参数，因此画面最初可能没有明显变化。对比时可以在预览中改变照度或光源颜色，再开启 IPA 并等待算法重新稳定。此开关控制哪些算法取决于当前 IPA 配置。

手动控制 ISP
------------

初始化后，``sensor.get_isp(block)`` 返回驱动当前参数的字典；不支持该处理模块时返回 ``None``。摄像头驱动没有实现读取接口，或固件针对的芯片版本不具备相应硬件时，都属于不支持：``lsc`` 需要 ESP32-P4 v1.0 及以上，``blc`` 需要 v3.0 及以上；而 SC2336 等传感器的曝光是只写的，因此 ``sensor.get_isp("exposure")`` 通常返回 ``None``，但仍可通过 ``sensor.set_isp("exposure", value=...)`` 设置曝光。手动写入前先调用 ``sensor.set_ipa(False)``。``sensor.set_isp(block, **params)`` 修改一个处理模块，未指定的字段沿用当前配置。IPA 运行期间写入会抛出 ``OSError``，避免算法覆盖手动参数。

.. code-block:: python

   sensor.reset()
   sensor.skip_frames(time=2000)
   sensor.set_ipa(False)

   print(sensor.get_isp("wb"))
   sensor.set_isp("wb", enable=False)
   sensor.set_isp("ccm", enable=False)
   sensor.set_isp("gamma", enable=False)
   sensor.snapshot().flush()

   sensor.set_isp("wb", enable=True, rg=1.8, bg=1.4)
   sensor.set_isp("exposure", value=10000)  # 单位：微秒，按 100 微秒取整。
   sensor.set_isp("pixel_gain", value=2.0)  # 增益倍数。

   sensor.set_ipa(True)

处理模块名与字段沿用调参参数的命名：

.. list-table:: ISP 参数
   :header-rows: 1
   :widths: 18 82

   * - 模块
     - 字段
   * - ``exposure``
     - ``value``：曝光时间，单位微秒，须为正数。设备控制项以 100 微秒为单位计数，因此写入会就近取整到 100 微秒，取整后还须符合传感器上报的范围与步长，可用上下限即由此决定。例如 SC2336 在 1280x720 30fps 下大致接受 200 微秒到 33.2 毫秒，步长为 100 微秒。
   * - ``pixel_gain``
     - ``value``：正数增益倍数，在传感器范围内选择增益菜单中最接近的一项。
   * - ``demosaic``
     - ``enable``、``gradient_ratio``：梯度比例，非负数，范围由驱动的定点数格式决定。
   * - ``wb``
     - ``enable``、``rg``、``bg``：红、蓝通道增益倍数，范围为 0 到 1023/256。
   * - ``lsc``
     - ``enable``、``lsc_gain_size``、``gain_r``、``gain_gr``、``gain_gb``、``gain_b``：四个增益列表，长度均须与 ``get_isp("lsc")`` 返回的长度一致，取值范围为 0 到 1023/256。长度由采集尺寸决定。
   * - ``blc``
     - ``enable``、``stretch_enable``、``top_left_offset``、``top_right_offset``、``bottom_left_offset``、``bottom_right_offset``：黑电平偏移量，为 0 到 255 的整数。
   * - ``bf``
     - ``enable``、``level``\ （2–20）、``matrix``：按行排列的九个整数，范围为 0 到 15。
   * - ``sharpen``
     - ``enable``、``h_thresh``、``l_thresh``\ （0–255，低阈值不大于高阈值）、``h_coeff``、``m_coeff``\ （0 到 255/32）、``matrix``：按行排列的九个整数，范围为 0 到 31。
   * - ``ccm``
     - ``enable``、``matrix``：按行排列的九个有限数值，严格位于 -4 到 4 之间。
   * - ``gamma``
     - ``enable``、``x``、``y``：两个长度为 16 的整数列表，取值为 0 到 255。X 从隐含的零点开始递增，每段间隔必须为 2 的幂；最后一点须为 255，计算最后一段间隔时按 256 处理。

向已关闭的模块应用新参数时，应同时设置 ``enable=True``。仅传入 ``enable=False`` 会关闭该处理模块，而不替换已保存的表数据。白平衡增益在关闭后仍然保留，再次打开时恢复。Gamma 读回红通道曲线；设置 ``x`` 或 ``y`` 会将合成后的曲线应用于三个通道，仅修改开关则保留各通道曲线。LSC 表复制到摄像头持有的存储中，在垃圾回收及 IPA 切换后仍然有效；在驱动接受新表之前，摄像头会保留已生效的旧表，因此写入被拒绝时画面不受影响。定点数参数和传感器设置可能被驱动量化。

未知模块、无效数值及数组长度错误会在应用修改前被拒绝。不支持的写入会抛出 ``OSError``。设备错误可能留下部分已应用的配置，此时可调用 ``sensor.reset()`` 重新初始化。

``example/01-Camera/04-IPA/isp_preview.py`` 示例先配置输出格式，再关闭 IPA，然后持续预览，每 2 秒随机调整曝光、增益、白平衡增益和 CCM，并打印请求的曝光值及驱动读回的增益、白平衡增益和 CCM 参数，直到按 Ctrl-C 停止。可以修改 ``UPDATE_INTERVAL_MS`` 及各参数范围常量调整试验；当前传感器不接受的取值会被打印并跳过，不会中断预览。停止后，IPA 保持关闭并保留最后一次设置。

.. seealso::

   :doc:`../concepts/camera-pipeline` 说明了一帧图像如何从图像传感器到达 :py:class:`image.Image`\ ；:doc:`../concepts/image-model` 介绍像素格式与 色彩空间。

   可运行示例：``example/01-Camera/00-Snapshot``\ （采集并保存）与 ``example/01-Camera/03-MJPEG``\ （Wi-Fi MJPEG 串流）。

.. include:: _generated/sensor.rst
