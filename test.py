import cv2
import numpy as np

# Параметры кадра
width = 640  # замените на ширину вашего изображения
height = 480  # замените на высоту вашего изображения
pixel_format = "YUYV"  # формат пикселей, например YUYV или RGB

# Размер одного кадра в байтах
frame_size = width * height * 2  # *2, так как YUYV использует 2 байта на пиксель

# Открываем файл с сырыми кадрами
with open("file.raw", "rb") as f:
    raw_data = f.read()

# Разделяем файл на кадры
frames = [raw_data[i:i+frame_size] for i in range(0, len(raw_data), frame_size)]

for i, frame in enumerate(frames):
    # Преобразование YUYV в RGB
    yuyv = np.frombuffer(frame, dtype=np.uint8).reshape((height, width, 2))
    rgb = cv2.cvtColor(yuyv, cv2.COLOR_YUV2BGR_YUYV)

    # Отображение кадра
    cv2.imshow(f"Frame {i+1}", rgb)
    from time import sleep

    sleep(3);

cv2.destroyAllWindows()
