import json
import queue
import threading
import time
import tkinter as tk
from collections import deque
from tkinter import messagebox, ttk

try:
    import serial
    from serial.tools import list_ports
except ImportError as exc:
    raise SystemExit("缺少 pyserial，请先运行：pip install pyserial") from exc

try:
    from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
    from matplotlib.figure import Figure
except ImportError as exc:
    raise SystemExit("缺少 matplotlib，请先运行：pip install matplotlib") from exc


MAX_POINTS = 120
BAUDRATE = 115200


STATE_TEXT = {
    "RUN": "正常运行",
    "DISABLED": "未使能",
    "ESTOP": "急停",
    "FAILSAFE": "遥控失联",
}


class TelemetryMonitor(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("轮式机器人运行状态监控")
        self.geometry("1120x700")
        self.minsize(960, 620)
        self.configure(bg="#111827")

        self.serial_port = None
        self.reader_thread = None
        self.reader_running = False
        self.data_queue = queue.Queue()
        self.start_time = time.monotonic()

        self.x_data = deque(maxlen=MAX_POINTS)
        self.left_data = deque(maxlen=MAX_POINTS)
        self.right_data = deque(maxlen=MAX_POINTS)
        self.speed_data = deque(maxlen=MAX_POINTS)

        self.value_vars = {
            "left": tk.StringVar(value="-- rpm"),
            "right": tk.StringVar(value="-- rpm"),
            "speed": tk.StringVar(value="-- rpm"),
            "state": tk.StringVar(value="未连接"),
            "height": tk.StringVar(value="-- mm"),
        }

        self._build_ui()
        self.refresh_ports()
        self.after(100, self._poll_data_queue)
        self.protocol("WM_DELETE_WINDOW", self.on_close)

    def _build_ui(self):
        style = ttk.Style()
        style.theme_use("clam")
        style.configure("TCombobox", fieldbackground="#f9fafb", background="#f9fafb")

        top = tk.Frame(self, bg="#111827")
        top.pack(fill=tk.X, padx=18, pady=(16, 8))

        title = tk.Label(
            top,
            text="轮式机器人运行状态监控",
            bg="#111827",
            fg="#f9fafb",
            font=("Microsoft YaHei UI", 20, "bold"),
        )
        title.pack(side=tk.LEFT)

        controls = tk.Frame(top, bg="#111827")
        controls.pack(side=tk.RIGHT)

        self.port_combo = ttk.Combobox(controls, width=18, state="readonly")
        self.port_combo.pack(side=tk.LEFT, padx=(0, 8))

        self.refresh_button = tk.Button(
            controls,
            text="刷新",
            command=self.refresh_ports,
            bg="#374151",
            fg="#f9fafb",
            activebackground="#4b5563",
            activeforeground="#ffffff",
            relief=tk.FLAT,
            padx=14,
            pady=8,
        )
        self.refresh_button.pack(side=tk.LEFT, padx=(0, 8))

        self.connect_button = tk.Button(
            controls,
            text="连接",
            command=self.toggle_connection,
            bg="#0f766e",
            fg="#ffffff",
            activebackground="#115e59",
            activeforeground="#ffffff",
            relief=tk.FLAT,
            padx=18,
            pady=8,
        )
        self.connect_button.pack(side=tk.LEFT)

        body = tk.Frame(self, bg="#111827")
        body.pack(fill=tk.BOTH, expand=True, padx=18, pady=(0, 18))
        body.columnconfigure(0, weight=0)
        body.columnconfigure(1, weight=1)
        body.rowconfigure(0, weight=1)

        cards = tk.Frame(body, bg="#111827", width=310)
        cards.grid(row=0, column=0, sticky="nsw", padx=(0, 14))
        cards.grid_propagate(False)

        self._add_card(cards, "左轮转速", self.value_vars["left"], "#38bdf8")
        self._add_card(cards, "右轮转速", self.value_vars["right"], "#f97316")
        self._add_card(cards, "当前移动速度", self.value_vars["speed"], "#22c55e")
        self._add_card(cards, "运行状态", self.value_vars["state"], "#facc15")
        self._add_card(cards, "升降机构高度", self.value_vars["height"], "#a78bfa")

        chart_host = tk.Frame(body, bg="#f9fafb")
        chart_host.grid(row=0, column=1, sticky="nsew")

        self.figure = Figure(figsize=(7, 5), dpi=100, facecolor="#f9fafb")
        self.axis = self.figure.add_subplot(111)
        self.axis.set_title("实时速度曲线", fontname="Microsoft YaHei UI")
        self.axis.set_xlabel("时间 / s", fontname="Microsoft YaHei UI")
        self.axis.set_ylabel("转速 / rpm", fontname="Microsoft YaHei UI")
        self.axis.grid(True, color="#d1d5db", linewidth=0.8)

        (self.left_line,) = self.axis.plot([], [], color="#0284c7", linewidth=2, label="左轮")
        (self.right_line,) = self.axis.plot([], [], color="#ea580c", linewidth=2, label="右轮")
        (self.speed_line,) = self.axis.plot([], [], color="#16a34a", linewidth=2, label="总速度")
        self.axis.legend(loc="upper left", prop={"family": "Microsoft YaHei UI"})

        self.canvas = FigureCanvasTkAgg(self.figure, master=chart_host)
        self.canvas.get_tk_widget().pack(fill=tk.BOTH, expand=True, padx=10, pady=10)

    def _add_card(self, parent, title, value_var, color):
        card = tk.Frame(parent, bg="#1f2937", highlightthickness=1, highlightbackground="#374151")
        card.pack(fill=tk.X, pady=(0, 12))

        label = tk.Label(
            card,
            text=title,
            bg="#1f2937",
            fg="#d1d5db",
            font=("Microsoft YaHei UI", 11),
            anchor="w",
        )
        label.pack(fill=tk.X, padx=16, pady=(12, 2))

        value = tk.Label(
            card,
            textvariable=value_var,
            bg="#1f2937",
            fg=color,
            font=("Consolas", 22, "bold"),
            anchor="w",
        )
        value.pack(fill=tk.X, padx=16, pady=(0, 14))

    def refresh_ports(self):
        ports = [port.device for port in list_ports.comports()]
        self.port_combo["values"] = ports
        if ports and not self.port_combo.get():
            self.port_combo.set(ports[0])

    def toggle_connection(self):
        if self.serial_port:
            self.disconnect()
        else:
            self.connect()

    def connect(self):
        port = self.port_combo.get().strip()
        if not port:
            messagebox.showwarning("未选择串口", "请先选择 STM32 虚拟串口。")
            return

        try:
            self.serial_port = serial.Serial(port, BAUDRATE, timeout=0.1)
        except serial.SerialException as exc:
            messagebox.showerror("连接失败", str(exc))
            self.serial_port = None
            return

        self.reader_running = True
        self.reader_thread = threading.Thread(target=self._reader_loop, daemon=True)
        self.reader_thread.start()
        self.connect_button.configure(text="断开", bg="#b91c1c", activebackground="#991b1b")
        self.value_vars["state"].set("等待数据")

    def disconnect(self):
        self.reader_running = False
        if self.reader_thread and self.reader_thread.is_alive():
            self.reader_thread.join(timeout=0.5)
        self.reader_thread = None

        if self.serial_port:
            try:
                self.serial_port.close()
            except serial.SerialException:
                pass
        self.serial_port = None
        self.connect_button.configure(text="连接", bg="#0f766e", activebackground="#115e59")
        self.value_vars["state"].set("未连接")

    def _reader_loop(self):
        while self.reader_running and self.serial_port:
            try:
                raw = self.serial_port.readline()
            except serial.SerialException as exc:
                self.data_queue.put(("error", str(exc)))
                break

            if not raw:
                continue

            line = raw.decode("utf-8", errors="ignore").strip()
            if not line:
                continue

            try:
                data = json.loads(line)
            except json.JSONDecodeError:
                continue

            self.data_queue.put(("data", data))

    def _poll_data_queue(self):
        try:
            while True:
                kind, payload = self.data_queue.get_nowait()
                if kind == "error":
                    messagebox.showerror("串口错误", payload)
                    self.disconnect()
                elif kind == "data":
                    self._update_from_data(payload)
        except queue.Empty:
            pass

        self.after(100, self._poll_data_queue)

    def _update_from_data(self, data):
        left = int(data.get("left_rpm", 0))
        right = int(data.get("right_rpm", 0))
        speed = int(data.get("speed_rpm", 0))
        height = int(data.get("height_mm", -1))
        state = str(data.get("state", "UNKNOWN"))

        self.value_vars["left"].set(f"{left} rpm")
        self.value_vars["right"].set(f"{right} rpm")
        self.value_vars["speed"].set(f"{speed} rpm")
        self.value_vars["state"].set(STATE_TEXT.get(state, state))
        self.value_vars["height"].set("-- mm" if height < 0 else f"{height} mm")

        now = time.monotonic() - self.start_time
        self.x_data.append(now)
        self.left_data.append(left)
        self.right_data.append(right)
        self.speed_data.append(speed)
        self._redraw_chart()

    def _redraw_chart(self):
        self.left_line.set_data(self.x_data, self.left_data)
        self.right_line.set_data(self.x_data, self.right_data)
        self.speed_line.set_data(self.x_data, self.speed_data)

        if self.x_data:
            self.axis.set_xlim(max(0, self.x_data[0]), max(10, self.x_data[-1]))

        all_values = list(self.left_data) + list(self.right_data) + list(self.speed_data)
        if all_values:
            low = min(all_values)
            high = max(all_values)
            margin = max(5, int((high - low) * 0.2))
            self.axis.set_ylim(low - margin, high + margin)

        self.canvas.draw_idle()

    def on_close(self):
        self.disconnect()
        self.destroy()


if __name__ == "__main__":
    app = TelemetryMonitor()
    app.mainloop()
