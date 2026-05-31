"""
轮式机器人上位机监控程序。

快速定位（行号对应当前文件版本；后续大量插入代码后需要重新核对）：
- 串口与数据窗口：第 52 行，MAX_POINTS / BAUDRATE。
- 速度计算常量：第 56 行，WHEEL_RADIUS_M / RPM_TO_MPS。
- 图表坐标轴上限：第 60 行，MAX_RPM_DISPLAY / MAX_SPEED_MPS_DISPLAY。
- 图表外边距和 DPI：第 63 行，CHART_PADDING / CHART_DPI。
- 图表坐标轴边距：第 67 行，CHART_SUBPLOT。
- 图表文字和线宽：第 74 行，CHART_*_SIZE / CHART_*_WIDTH。
- 字体大小：第 85 行，APP_TITLE_FONT / CARD_*_FONT / BUTTON_FONT。
- 颜色主题：第 93 行，APP_BG / PANEL_BG / CARD_BG / LINE_COLORS。
- 数据记录参数：第 119 行，DATA_DIR / CSV_FIELDS。
- 程序窗口图标：第 118 行，APP_ICON_PATH。
- 分辨率和 DPI：第 225 行 _configure_dpi_scaling，第 233 行 _configure_window_size。
- 图表尺寸和坐标轴边距：第 343 行 Figure / subplots_adjust，第 433 行 _resize_chart。
- 状态文字显示：第 168 行，STATE_TEXT。
"""

import csv
import ctypes
import json
import math
import os
import queue
import sys
import threading
import time
import tkinter as tk
from collections import deque
from datetime import datetime
from pathlib import Path
from tkinter import messagebox, ttk

try:
    import serial
    from serial.tools import list_ports
except ImportError as exc:
    raise SystemExit("缺少 pyserial，请先运行：pip install pyserial") from exc

try:
    import matplotlib as mpl
    from matplotlib import font_manager
    from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
    from matplotlib.figure import Figure
    from matplotlib.font_manager import FontProperties
except ImportError as exc:
    raise SystemExit("缺少 matplotlib，请先运行：pip install matplotlib") from exc


# ===== 串口、采样和速度计算参数 =====
# MAX_POINTS 控制图表保留的数据点数量；数值越大，曲线历史越长，但刷新开销也会增加。
MAX_POINTS = 120
# BAUDRATE 必须和单片机 USB CDC 串口输出速率保持一致。
BAUDRATE = 115200
# WHEEL_RADIUS_M 是轮半径，单位 m；当前实测/给定值为 75 mm。
WHEEL_RADIUS_M = 0.075
# RPM_TO_MPS 是 rpm 到 m/s 的换算系数；当前移动速度由左右轮绝对转速平均值换算得到。
RPM_TO_MPS = 2 * math.pi * WHEEL_RADIUS_M / 60
# 图表坐标轴显示上限；超过上限的数据仍会被接收，但曲线会被坐标轴裁掉。
MAX_RPM_DISPLAY = 50
MAX_SPEED_MPS_DISPLAY = 1.0
# 图表控件和外框之间的像素留白；数值越小，图表越贴近外框。
CHART_PADDING = 4
# Matplotlib 渲染 DPI；降低它可以让图表文字和刻度更紧凑，避免显得过大。
CHART_DPI = 90
# 图表坐标轴边距；right/bottom 仍需给右侧 m/s 轴和底部时间轴留出空间。
CHART_SUBPLOT = {
    "left": 0.13,
    "right": 0.84,
    "top": 0.86,
    "bottom": 0.20,
}
# 图表文字和线宽；这里只影响右侧曲线图，不影响左侧信息卡片。
CHART_TITLE_SIZE = 26
CHART_LABEL_SIZE = 24
CHART_TICK_SIZE = 22
CHART_LEGEND_SIZE = 22
CHART_AXIS_LINE_WIDTH = 3.0
CHART_TICK_WIDTH = 2.6
CHART_GRID_WIDTH = 1.4
CHART_CURVE_WIDTH = 3.8

# ===== 字体调整区 =====
# 这里集中控制主标题、信息卡片、状态文字和按钮字体；字号过大时会挤压右侧图表区域。
APP_TITLE_FONT = ("Microsoft YaHei UI", 23, "bold")
CARD_LABEL_FONT = ("Microsoft YaHei UI", 13)
CARD_VALUE_FONT = ("Segoe UI", 26, "bold")
CARD_STATE_FONT = ("Microsoft YaHei UI", 26, "bold")
BUTTON_FONT = ("Microsoft YaHei UI", 11, "bold")

# ===== 颜色调整区 =====
# 页面背景、卡片、图表和文字颜色统一放在这里，避免分散到界面构建代码中。
APP_BG = "#0b1220"
PANEL_BG = "#111827"
CARD_BG = "#172033"
CARD_BORDER = "#263244"
TEXT_PRIMARY = "#f8fafc"
TEXT_SECONDARY = "#cbd5e1"
TEXT_MUTED = "#94a3b8"
CHART_BG = "#0f172a"
CHART_GRID = "#263244"
CHART_SPINE = "#475569"

LINE_COLORS = {
    # 三条实时曲线和左侧数值卡片共用这一组颜色。
    "left": "#7dd3fc",
    "right": "#fbbf24",
    "speed": "#86efac",
    "height": "#c4b5fd",
    "state": "#fde68a",
}


# ===== 数据记录参数 =====
# 记录文件固定放在当前程序/可执行文件同级 data 目录下；实际数据目录已在 .gitignore 中忽略。
PROGRAM_DIR = Path(sys.executable).resolve().parent if getattr(sys, "frozen", False) else Path(__file__).resolve().parent
BUNDLED_DIR = Path(getattr(sys, "_MEIPASS", PROGRAM_DIR))
APP_ICON_PATH = BUNDLED_DIR / "assets" / "app_icon.ico"
DATA_DIR = PROGRAM_DIR / "data"
RAW_LOG_FILENAME = "raw.jsonl"
CSV_LOG_FILENAME = "telemetry.csv"
SESSION_INFO_FILENAME = "session_info.json"
CSV_FIELDS = [
    "pc_time",
    "time_s",
    "left_rpm",
    "right_rpm",
    "speed_mps",
    "state",
    "height_mm",
    "left_torque",
    "right_torque",
]


def get_chinese_font():
    # Matplotlib 不一定会自动使用中文字体；这里按 Windows 常见字体顺序兜底。
    candidates = [
        r"C:\Windows\Fonts\msyh.ttc",
        r"C:\Windows\Fonts\simhei.ttf",
        r"C:\Windows\Fonts\simsun.ttc",
    ]
    for path in candidates:
        if os.path.exists(path):
            font_manager.fontManager.addfont(path)
            return FontProperties(fname=path)
    return FontProperties(family=["Microsoft YaHei", "SimHei", "SimSun", "sans-serif"])


CJK_FONT = get_chinese_font()
mpl.rcParams["axes.unicode_minus"] = False


def enable_high_dpi_awareness():
    # Windows 高 DPI 感知：减少界面模糊。失败时直接跳过，不影响串口监控功能。
    if os.name != "nt":
        return

    try:
        ctypes.windll.shcore.SetProcessDpiAwareness(1)
    except (AttributeError, OSError):
        try:
            ctypes.windll.user32.SetProcessDPIAware()
        except (AttributeError, OSError):
            pass


STATE_TEXT = {
    # 固件发来的英文状态码在这里转成界面中文；新增状态时优先改这里。
    "RUN": "正常运行",
    "DISABLED": "未使能",
    "ESTOP": "急停",
    "FAILSAFE": "遥控失联",
}


class TelemetryMonitor(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("轮式机器人运行状态监控")
        self._set_window_icon()
        self.configure(bg=APP_BG)
        self._configure_dpi_scaling()
        self._configure_window_size()

        self.serial_port = None
        self.reader_thread = None
        self.reader_running = False
        self.data_queue = queue.Queue()
        self.log_queue = queue.Queue()
        self.start_time = time.monotonic()
        self.chart_font = CJK_FONT
        self._last_chart_size = None
        self.is_recording = False
        self.log_thread = None
        self.current_session_dir = None

        self.x_data = deque(maxlen=MAX_POINTS)
        self.left_data = deque(maxlen=MAX_POINTS)
        self.right_data = deque(maxlen=MAX_POINTS)
        self.speed_data = deque(maxlen=MAX_POINTS)

        self.value_vars = {
            "left": tk.StringVar(value="-- rpm"),
            "right": tk.StringVar(value="-- rpm"),
            "speed": tk.StringVar(value="-- m/s"),
            "state": tk.StringVar(value="未连接"),
            "height": tk.StringVar(value="-- mm"),
        }

        self._build_ui()
        self.refresh_ports()
        self.after(100, self._poll_data_queue)
        self.protocol("WM_DELETE_WINDOW", self.on_close)

    def _set_window_icon(self):
        if not APP_ICON_PATH.exists():
            return

        try:
            self.iconbitmap(str(APP_ICON_PATH))
        except tk.TclError:
            pass

    def _configure_dpi_scaling(self):
        # 分辨率调整：限制 Tk 缩放上限，避免高 DPI 屏幕把控件撑到窗口外。
        try:
            scale = self.winfo_fpixels("1i") / 72.0
            self.tk.call("tk", "scaling", max(1.0, min(scale, 1.25)))
        except tk.TclError:
            pass

    def _configure_window_size(self):
        # 分辨率调整：根据屏幕尺寸设置默认窗口，给系统标题栏和任务栏留出空间。
        screen_width = self.winfo_screenwidth()
        screen_height = self.winfo_screenheight()
        width = min(1500, max(1080, screen_width - 80))
        height = min(820, max(640, screen_height - 180))
        self.geometry(f"{width}x{height}+40+30")
        self.minsize(1080, 640)

    def _build_ui(self):
        # 界面结构：上方为串口控制，左侧为关键数值卡片，右侧为实时曲线图。
        style = ttk.Style()
        style.theme_use("clam")
        style.configure(
            "Telemetry.TCombobox",
            fieldbackground=CARD_BG,
            background=CARD_BG,
            foreground=TEXT_PRIMARY,
            arrowcolor=TEXT_PRIMARY,
            bordercolor=CARD_BORDER,
            lightcolor=CARD_BORDER,
            darkcolor=CARD_BORDER,
            padding=6,
        )
        style.map("Telemetry.TCombobox", fieldbackground=[("readonly", CARD_BG)])

        top = tk.Frame(self, bg=APP_BG)
        top.pack(fill=tk.X, padx=18, pady=(16, 8))

        title = tk.Label(
            top,
            text="轮式机器人运行状态监控",
            bg=APP_BG,
            fg=TEXT_PRIMARY,
            font=APP_TITLE_FONT,
        )
        title.pack(side=tk.LEFT)

        controls = tk.Frame(top, bg=APP_BG)
        controls.pack(side=tk.RIGHT)

        self.port_combo = ttk.Combobox(controls, width=18, state="readonly", style="Telemetry.TCombobox")
        self.port_combo.pack(side=tk.LEFT, padx=(0, 8))

        self.refresh_button = tk.Button(
            controls,
            text="刷新",
            command=self.refresh_ports,
            bg="#243044",
            fg=TEXT_PRIMARY,
            activebackground="#334155",
            activeforeground=TEXT_PRIMARY,
            font=BUTTON_FONT,
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
            fg=TEXT_PRIMARY,
            activebackground="#0d9488",
            activeforeground=TEXT_PRIMARY,
            font=BUTTON_FONT,
            relief=tk.FLAT,
            padx=18,
            pady=8,
        )
        self.connect_button.pack(side=tk.LEFT)

        self.record_button = tk.Button(
            controls,
            text="记录",
            command=self.toggle_recording,
            bg="#334155",
            fg=TEXT_PRIMARY,
            activebackground="#475569",
            activeforeground=TEXT_PRIMARY,
            font=BUTTON_FONT,
            relief=tk.FLAT,
            padx=18,
            pady=8,
        )
        self.record_button.pack(side=tk.LEFT, padx=(8, 0))

        body = tk.Frame(self, bg=APP_BG)
        body.pack(fill=tk.BOTH, expand=True, padx=18, pady=(0, 18))
        body.columnconfigure(0, weight=0)
        body.columnconfigure(1, weight=1)
        body.rowconfigure(0, weight=1)

        cards = tk.Frame(body, bg=APP_BG, width=340)
        # 卡片宽度会影响右侧图表剩余空间；左侧文字变大后可适当增加这里。
        cards.grid(row=0, column=0, sticky="nsw", padx=(0, 14))
        cards.grid_propagate(False)

        self._add_card(cards, "左轮转速", self.value_vars["left"], LINE_COLORS["left"])
        self._add_card(cards, "右轮转速", self.value_vars["right"], LINE_COLORS["right"])
        self._add_card(cards, "当前移动速度", self.value_vars["speed"], LINE_COLORS["speed"])
        self._add_card(cards, "运行状态", self.value_vars["state"], LINE_COLORS["state"], value_font=CARD_STATE_FONT)
        self._add_card(cards, "升降机构高度", self.value_vars["height"], LINE_COLORS["height"])

        chart_host = tk.Frame(body, bg=PANEL_BG, highlightthickness=1, highlightbackground=CARD_BORDER)
        chart_host.grid(row=0, column=1, sticky="nsew")
        chart_host.bind("<Configure>", self._resize_chart)

        self.figure = Figure(figsize=(5.8, 3.8), dpi=CHART_DPI, facecolor=PANEL_BG)
        # 图表边距：CHART_SUBPLOT 控制坐标轴实际占用比例，调大绘图区优先改这里。
        self.figure.subplots_adjust(**CHART_SUBPLOT)
        self.axis = self.figure.add_subplot(111)
        self.axis.set_facecolor(CHART_BG)
        self.axis.set_title(
            "实时速度曲线",
            fontproperties=self.chart_font,
            color=TEXT_PRIMARY,
            fontsize=CHART_TITLE_SIZE,
            fontweight="bold",
            pad=10,
        )
        self.axis.set_xlabel(
            "时间 / s",
            fontproperties=self.chart_font,
            color=TEXT_SECONDARY,
            fontsize=CHART_LABEL_SIZE,
            fontweight="bold",
            labelpad=10,
        )
        self.axis.set_ylabel(
            "转速 / rpm",
            fontproperties=self.chart_font,
            color=TEXT_SECONDARY,
            fontsize=CHART_LABEL_SIZE,
            fontweight="bold",
            labelpad=12,
        )
        self.axis.grid(True, color=CHART_GRID, linewidth=CHART_GRID_WIDTH, alpha=0.85)
        self.axis.axhline(0, color="#64748b", linewidth=CHART_AXIS_LINE_WIDTH, alpha=0.65)
        self.axis.set_xlim(0, 10)
        self.axis.set_ylim(0, MAX_RPM_DISPLAY)
        self.axis.tick_params(colors=TEXT_MUTED, labelsize=CHART_TICK_SIZE, width=CHART_TICK_WIDTH, length=8)
        for tick_label in self.axis.get_xticklabels() + self.axis.get_yticklabels():
            tick_label.set_fontweight("bold")
        for spine in self.axis.spines.values():
            spine.set_color(CHART_SPINE)
            spine.set_linewidth(CHART_AXIS_LINE_WIDTH)

        self.speed_axis = self.axis.twinx()
        self.speed_axis.set_ylabel(
            "移动速度 / m/s",
            fontproperties=self.chart_font,
            color=LINE_COLORS["speed"],
            fontsize=CHART_LABEL_SIZE,
            fontweight="bold",
            labelpad=12,
        )
        self.speed_axis.set_ylim(0, MAX_SPEED_MPS_DISPLAY)
        self.speed_axis.tick_params(
            colors=LINE_COLORS["speed"],
            labelsize=CHART_TICK_SIZE,
            width=CHART_TICK_WIDTH,
            length=8,
        )
        self._style_chart_ticks()
        for spine_name, spine in self.speed_axis.spines.items():
            spine.set_color(LINE_COLORS["speed"] if spine_name == "right" else CHART_SPINE)
            spine.set_linewidth(CHART_AXIS_LINE_WIDTH)

        (self.left_line,) = self.axis.plot([], [], color=LINE_COLORS["left"], linewidth=CHART_CURVE_WIDTH, label="左轮")
        (self.right_line,) = self.axis.plot([], [], color=LINE_COLORS["right"], linewidth=CHART_CURVE_WIDTH, label="右轮")
        (self.speed_line,) = self.speed_axis.plot([], [], color=LINE_COLORS["speed"], linewidth=CHART_CURVE_WIDTH, label="当前移动速度")
        legend = self.axis.legend(
            handles=[self.left_line, self.right_line, self.speed_line],
            loc="upper left",
            prop=self.chart_font,
            facecolor="#182235",
            edgecolor=CARD_BORDER,
            framealpha=0.92,
        )
        for text in legend.get_texts():
            text.set_color(TEXT_SECONDARY)
            text.set_fontsize(CHART_LEGEND_SIZE)
            text.set_fontweight("bold")

        self.canvas = FigureCanvasTkAgg(self.figure, master=chart_host)
        self.canvas.get_tk_widget().configure(bg=PANEL_BG, highlightthickness=0)
        self.canvas.get_tk_widget().pack(fill=tk.BOTH, expand=True, padx=CHART_PADDING, pady=CHART_PADDING)

    def _style_chart_ticks(self):
        # 坐标数字样式：实时刷新坐标范围后也要保持大号加粗。
        for tick_label in self.axis.get_xticklabels() + self.axis.get_yticklabels():
            tick_label.set_fontsize(CHART_TICK_SIZE)
            tick_label.set_fontweight("bold")
        for tick_label in self.speed_axis.get_yticklabels():
            tick_label.set_fontsize(CHART_TICK_SIZE)
            tick_label.set_fontweight("bold")

    def _resize_chart(self, event):
        # 图表自适应：窗口大小变化时，用容器实际尺寸重设 Figure，避免坐标轴被裁切。
        if not hasattr(self, "canvas"):
            return

        width = max(360, event.width - CHART_PADDING * 2)
        height = max(260, event.height - CHART_PADDING * 2)
        new_size = (width, height)
        if new_size == self._last_chart_size:
            return

        self._last_chart_size = new_size
        self.figure.set_size_inches(width / self.figure.dpi, height / self.figure.dpi, forward=True)
        # 保持和初始化一致的边距；如果底部/右侧标签再次被遮挡，优先调 bottom/right。
        self.figure.subplots_adjust(**CHART_SUBPLOT)
        self.canvas.draw_idle()

    def _add_card(self, parent, title, value_var, color, value_font=CARD_VALUE_FONT):
        # 左侧数值卡片：标题字号看 CARD_LABEL_FONT，数值字号看 CARD_VALUE_FONT/CARD_STATE_FONT。
        card = tk.Frame(parent, bg=CARD_BG, highlightthickness=1, highlightbackground=CARD_BORDER)
        card.pack(fill=tk.X, pady=(0, 12))

        label = tk.Label(
            card,
            text=title,
            bg=CARD_BG,
            fg=TEXT_SECONDARY,
            font=CARD_LABEL_FONT,
            anchor="w",
        )
        label.pack(fill=tk.X, padx=18, pady=(14, 3))

        value = tk.Label(
            card,
            textvariable=value_var,
            bg=CARD_BG,
            fg=color,
            font=value_font,
            anchor="w",
        )
        value.pack(fill=tk.X, padx=18, pady=(0, 16))

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

    def toggle_recording(self):
        if self.is_recording:
            self.stop_recording()
        else:
            self.start_recording()

    def start_recording(self):
        if self.is_recording:
            return

        try:
            DATA_DIR.mkdir(parents=True, exist_ok=True)
            session_dir = self._make_session_dir()
            session_dir.mkdir()
            self._write_session_info(session_dir)
        except OSError as exc:
            messagebox.showerror("记录失败", str(exc))
            return

        self.log_queue = queue.Queue()
        self.current_session_dir = session_dir
        self.is_recording = True
        self.log_thread = threading.Thread(target=self._log_writer_loop, args=(session_dir, self.log_queue), daemon=True)
        self.log_thread.start()
        self.record_button.configure(text="停止", bg="#b45309", activebackground="#d97706")

    def stop_recording(self):
        if not self.is_recording:
            return

        self.is_recording = False
        self.log_queue.put(None)
        if self.log_thread and self.log_thread.is_alive() and threading.current_thread() is not self.log_thread:
            self.log_thread.join(timeout=1.0)

        self.log_thread = None
        self.record_button.configure(text="记录", bg="#334155", activebackground="#475569")

    def _make_session_dir(self):
        timestamp = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
        session_dir = DATA_DIR / timestamp
        index = 1
        while session_dir.exists():
            session_dir = DATA_DIR / f"{timestamp}_{index:02d}"
            index += 1
        return session_dir

    def _write_session_info(self, session_dir):
        info = {
            "created_at": datetime.now().isoformat(timespec="seconds"),
            "port": self.port_combo.get().strip() or None,
            "baudrate": BAUDRATE,
            "wheel_radius_m": WHEEL_RADIUS_M,
            "raw_log": RAW_LOG_FILENAME,
            "csv_log": CSV_LOG_FILENAME,
            "csv_fields": CSV_FIELDS,
            "missing_value_policy": "JSONL 使用 null；CSV 使用空单元格。单片机实际发送 0 时仍记录为 0。",
        }
        with (session_dir / SESSION_INFO_FILENAME).open("w", encoding="utf-8") as info_file:
            json.dump(info, info_file, ensure_ascii=False, indent=2)

    def _log_writer_loop(self, session_dir, log_queue):
        raw_path = session_dir / RAW_LOG_FILENAME
        csv_path = session_dir / CSV_LOG_FILENAME

        try:
            with raw_path.open("a", encoding="utf-8", newline="") as raw_file, csv_path.open(
                "a", encoding="utf-8-sig", newline=""
            ) as csv_file:
                writer = csv.DictWriter(csv_file, fieldnames=CSV_FIELDS)
                writer.writeheader()

                while True:
                    record = log_queue.get()
                    if record is None:
                        break

                    raw_file.write(json.dumps(record["raw"], ensure_ascii=False) + "\n")
                    writer.writerow({field: self._csv_value(record["csv"].get(field)) for field in CSV_FIELDS})
                    raw_file.flush()
                    csv_file.flush()
        except OSError as exc:
            self.data_queue.put(("log_error", str(exc)))

    @staticmethod
    def _csv_value(value):
        return "" if value is None else value

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
        self.connect_button.configure(text="断开", bg="#b91c1c", activebackground="#dc2626")
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
        self.connect_button.configure(text="连接", bg="#0f766e", activebackground="#0d9488")
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
                elif kind == "log_error":
                    messagebox.showerror("记录错误", payload)
                    self.stop_recording()
                elif kind == "data":
                    self._update_from_data(payload)
        except queue.Empty:
            pass

        self.after(100, self._poll_data_queue)

    @staticmethod
    def _optional_float(data, key):
        if key not in data:
            return None

        value = data.get(key)
        if value is None or value == "":
            return None

        try:
            number = float(value)
        except (TypeError, ValueError):
            return None

        return None if math.isnan(number) else number

    @staticmethod
    def _optional_int(data, key):
        number = TelemetryMonitor._optional_float(data, key)
        return None if number is None else int(number)

    @staticmethod
    def _optional_abs_int(data, key):
        number = TelemetryMonitor._optional_float(data, key)
        return None if number is None else abs(int(number))

    def _queue_log_record(self, raw_data, normalized_data):
        if not self.is_recording:
            return

        raw_record = {
            "pc_time": normalized_data["pc_time"],
            "time_s": normalized_data["time_s"],
            "payload": raw_data,
            "normalized": normalized_data,
        }
        self.log_queue.put({"raw": raw_record, "csv": normalized_data})

    def _update_from_data(self, data):
        left = self._optional_abs_int(data, "left_rpm")
        right = self._optional_abs_int(data, "right_rpm")
        speed = None if left is None or right is None else ((left + right) / 2) * RPM_TO_MPS
        height = self._optional_int(data, "height_mm")
        if height is not None and height < 0:
            height = None
        state = str(data["state"]) if data.get("state") not in (None, "") else None

        self.value_vars["left"].set("-- rpm" if left is None else f"{left} rpm")
        self.value_vars["right"].set("-- rpm" if right is None else f"{right} rpm")
        self.value_vars["speed"].set("-- m/s" if speed is None else f"{speed:.3f} m/s")
        self.value_vars["state"].set("未知" if state is None else STATE_TEXT.get(state, state))
        self.value_vars["height"].set("-- mm" if height is None else f"{height} mm")

        now = time.monotonic() - self.start_time
        self.x_data.append(now)
        self.left_data.append(math.nan if left is None else left)
        self.right_data.append(math.nan if right is None else right)
        self.speed_data.append(math.nan if speed is None else speed)
        self._queue_log_record(
            data,
            {
                "pc_time": datetime.now().isoformat(timespec="milliseconds"),
                "time_s": round(now, 3),
                "left_rpm": left,
                "right_rpm": right,
                "speed_mps": None if speed is None else round(speed, 6),
                "state": state,
                "height_mm": height,
                "left_torque": self._optional_float(data, "left_torque"),
                "right_torque": self._optional_float(data, "right_torque"),
            },
        )
        self._redraw_chart()

    def _redraw_chart(self):
        self.left_line.set_data(self.x_data, self.left_data)
        self.right_line.set_data(self.x_data, self.right_data)
        self.speed_line.set_data(self.x_data, self.speed_data)

        if self.x_data:
            self.axis.set_xlim(max(0, self.x_data[0]), max(10, self.x_data[-1]))

        self.axis.set_ylim(0, MAX_RPM_DISPLAY)
        self.speed_axis.set_ylim(0, MAX_SPEED_MPS_DISPLAY)
        self._style_chart_ticks()

        self.canvas.draw_idle()

    def on_close(self):
        self.stop_recording()
        self.disconnect()
        self.destroy()


if __name__ == "__main__":
    enable_high_dpi_awareness()
    app = TelemetryMonitor()
    app.mainloop()
