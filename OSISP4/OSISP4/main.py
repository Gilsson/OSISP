from matplotlib import pyplot as plt
import numpy as np

# Чтение данных из файла и группировка по номеру запроса
table = {}
with open("time.txt", "r") as file:
    lines = file.readlines()

    for line in lines:
        splits = line.strip().split(" ")
        request_num = int(splits[0])
        stage_index = int(splits[1])
        start_time = int(splits[2])
        end_time = int(splits[3])

        # Добавляем информацию о каждом этапе для соответствующего запроса
        if request_num in table:
            table[request_num].append((stage_index, start_time, end_time))
        else:
            table[request_num] = [(stage_index, start_time, end_time)]

# Вывод структуры данных для отладки
for req_num, stages in table.items():
    print(f"Request {req_num}: {stages}")

# Построение графика
fig, ax = plt.subplots()

# Цвета для разных этапов
colors = ["tab:blue", "tab:orange", "tab:green", "tab:red"] * 3

# Построение broken_barh для каждого запроса
for request_num, stages in table.items():
    for stage_index, start_time, end_time in stages:
        duration = end_time - start_time
        # Используем номер запроса в качестве значения по оси y
        ax.broken_barh(
            [(start_time, duration)],
            (request_num - 0.4, 0.8),
            facecolors=colors[stage_index],
        )

# Подписи осей и оформление графика
ax.set_xlabel("Time (ms)")
ax.set_ylabel("Request Number")
ax.set_title("Stages of Requests Over Time")

# Сохранение графика
plt.savefig("time.png")
