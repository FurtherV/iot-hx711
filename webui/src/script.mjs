import uPlot from "uplot";
import firmwareLogo from "./firmware-logo.svg?raw";

const SAMPLE_HISTORY_LIMIT = 60;
const SAMPLE_Y_MIN = 0;
const SAMPLE_Y_MAX = 5500;
const ACTIVE_TAB_STORAGE_KEY = "iot_hx711.activeTab";

const state = {
  config: {
    sampleIntervalMs: 1000,
    sampleIntervalMinMs: 100,
    sampleIntervalMaxMs: 10000,
  },
  info: null,
  partitions: [],
  sample: null,
  samplePollTimer: null,
  samplePlot: null,
  samplePlotData: [[], []],
  wifi: null,
};

const $ = (selector) => document.querySelector(selector);

function setText(selector, value) {
  const node = $(selector);
  if (node) {
    node.textContent = value ?? "-";
    if (value) {
      node.classList.remove("hidden");
    }
  }
}

function showAlert(title, message) {
  setText("#dialogTitle", title);
  setText("#dialogMessage", message);

  const dialog = $("#appDialog");
  if (dialog?.showModal) {
    dialog.showModal();
    return;
  }

  alert(`${title}\n\n${message}`);
}

function connectionLabel(wifi) {
  if (!wifi) {
    return "Unknown";
  }
  if (wifi.connected) {
    return `STA ${wifi.ssid || ""}`.trim();
  }
  if (wifi.softapActive) {
    return `SoftAP ${wifi.apSsid || ""}`.trim();
  }
  return "Disconnected";
}

function renderInfo(info) {
  state.info = info;
  const wifi = info.wifi || {};

  const details = $("#details");
  details.innerHTML = "";
  const rows = [
    ["Application", info.appName],
    ["Version", info.appVersion],
    ["IDF", info.idfVersion],
    ["Target", info.target],
    ["Free heap", `${info.freeHeap} bytes`],
    ["Minimum free heap", `${info.minFreeHeap} bytes`],
    ["Active partition", info.partition],
    ["WiFi", connectionLabel(wifi)],
    ["Stored credentials", wifi.hasCredentials ? "Yes" : "No"],
    ["IP address", wifi.ip || "-"],
  ];

  for (const [label, value] of rows) {
    const term = document.createElement("dt");
    term.textContent = label;
    const description = document.createElement("dd");
    description.textContent = value ?? "-";
    details.append(term, description);
  }
}

function renderWifi(wifi) {
  state.wifi = wifi;
  $("#ssid").value = wifi.ssid || "";
  $("#password").value = wifi.password || "";
  renderSsidOptions(wifi.availableSsids || []);
  setText("#wifiStatusText", "");
}

function renderSsidOptions(ssids) {
  const options = $("#ssidOptions");
  options.innerHTML = "";

  const seen = new Set();
  for (const ssid of ssids) {
    if (typeof ssid !== "string" || ssid === "" || seen.has(ssid)) {
      continue;
    }

    seen.add(ssid);
    const option = document.createElement("option");
    option.value = ssid;
    options.append(option);
  }
}

function renderConfig(config) {
  state.config = {
    sampleIntervalMs: config.sampleIntervalMs ?? 1000,
    sampleIntervalMinMs: config.sampleIntervalMinMs ?? 100,
    sampleIntervalMaxMs: config.sampleIntervalMaxMs ?? 10000,
  };

  const input = $("#sampleIntervalMs");
  input.min = state.config.sampleIntervalMinMs;
  input.max = state.config.sampleIntervalMaxMs;
  input.value = state.config.sampleIntervalMs;
  setText("#sampleIntervalRange", `Allowed range: ${state.config.sampleIntervalMinMs}-${state.config.sampleIntervalMaxMs} ms`);
}

function renderSample(sample) {
  state.sample = sample;
  const firstValue = sample.data?.[0];
  if (!Number.isFinite(firstValue?.value)) {
    return;
  }

  appendSamplePoint(firstValue.value);
}

function setupSamplePlot() {
  const container = $("#samplePlot");
  const options = {
    title: "Weight",
    width: Math.max(container.clientWidth, 320),
    height: 360,
    scales: {
      x: {
        time: true,
      },
      y: {
        range: () => [SAMPLE_Y_MIN, SAMPLE_Y_MAX],
      },
    },
    axes: [
      {},
      {
        label: "g",
        values: (_u, ticks) => ticks.map((value) => `${value}`),
      },
    ],
    series: [
      {},
      {
        label: "g",
        stroke: "#f58220",
        width: 2,
      },
    ],
  };

  state.samplePlot = new uPlot(options, state.samplePlotData, container);

  if ("ResizeObserver" in window) {
    const observer = new ResizeObserver(() => {
      state.samplePlot.setSize({
        width: Math.max(container.clientWidth, 320),
        height: 360,
      });
    });
    observer.observe(container);
  }
}

function appendSamplePoint(value) {
  state.samplePlotData[0].push(Date.now() / 1000);
  state.samplePlotData[1].push(value);

  while (state.samplePlotData[0].length > SAMPLE_HISTORY_LIMIT) {
    state.samplePlotData[0].shift();
    state.samplePlotData[1].shift();
  }

  state.samplePlot?.setData(state.samplePlotData);
}

function formatHex(value) {
  return Number.isFinite(value) ? `0x${value.toString(16).toUpperCase()}` : "-";
}

function formatSize(value) {
  if (!Number.isFinite(value)) {
    return "-";
  }
  return `${(value / 1024).toLocaleString(undefined, { maximumFractionDigits: 0 })} KB`;
}

function partitionRoles(partition) {
  const roles = [];
  if (partition.running) {
    roles.push("Running");
  }
  if (partition.boot) {
    roles.push("Boot");
  }
  if (partition.nextUpdate) {
    roles.push("Next OTA");
  }
  if (partition.encrypted) {
    roles.push("Encrypted");
  }
  return roles;
}

function renderPartitions(payload) {
  state.partitions = payload.partitions || [];

  const rows = $("#partitionRows");
  rows.innerHTML = "";

  if (state.partitions.length === 0) {
    const row = document.createElement("tr");
    const cell = document.createElement("td");
    cell.colSpan = 6;
    cell.textContent = "No partitions reported";
    row.append(cell);
    rows.append(row);
    setText("#partitionMessage", "No partitions reported.");
    return;
  }

  for (const partition of state.partitions) {
    const row = document.createElement("tr");
    const values = [
      partition.label,
      partition.type,
      partition.subtype,
      formatHex(partition.address),
      formatSize(partition.size),
    ];

    for (const value of values) {
      const cell = document.createElement("td");
      cell.textContent = value ?? "-";
      row.append(cell);
    }

    const roleCell = document.createElement("td");
    const roles = partitionRoles(partition);
    if (roles.length === 0) {
      roleCell.textContent = "-";
    } else {
      for (const role of roles) {
        const badge = document.createElement("span");
        badge.className = "role-badge";
        badge.textContent = role;
        roleCell.append(badge);
      }
    }
    row.append(roleCell);
    rows.append(row);
  }

  setText("#partitionMessage", `${state.partitions.length} partitions detected.`);
}

async function readJson(path, errorMessage) {
  const response = await fetch(path, { cache: "no-store" });
  if (!response.ok) {
    throw new Error(errorMessage);
  }
  return response.json();
}

async function postJson(path, errorMessage, payload = null) {
  const options = { method: "POST" };
  if (payload !== null) {
    options.headers = {
      "Content-Type": "application/json",
    };
    options.body = JSON.stringify(payload);
  }

  const response = await fetch(path, options);
  if (!response.ok) {
    throw new Error(await response.text() || errorMessage);
  }
  return response.json();
}

async function refreshInfo() {
  renderInfo(await readJson("/info", "Unable to load device information"));
}

async function refreshWifi() {
  renderWifi(await readJson("/wifi", "Unable to load WiFi settings"));
}

async function refreshConfig() {
  renderConfig(await readJson("/config", "Unable to load configuration"));
}

async function refreshPartitions() {
  renderPartitions(await readJson("/partitions", "Unable to load partition table"));
}

async function refreshSample() {
  renderSample(await readJson("/sample", "Sample unavailable"));
}

function setupSamplePolling() {
  const poll = async () => {
    try {
      await refreshSample();
    } catch (error) {
      console.warn(error.message);
    }
  };

  poll();
  if (state.samplePollTimer !== null) {
    clearInterval(state.samplePollTimer);
  }
  state.samplePollTimer = setInterval(poll, state.config.sampleIntervalMs);
}

function getStoredTab() {
  try {
    return sessionStorage.getItem(ACTIVE_TAB_STORAGE_KEY);
  } catch (error) {
    return null;
  }
}

function storeTab(tabId) {
  try {
    sessionStorage.setItem(ACTIVE_TAB_STORAGE_KEY, tabId);
  } catch (error) {
    // The UI remains usable when session storage is unavailable.
  }
}

function activateTab(tabId, persist = true) {
  const tab = document.querySelector(`.nav-tab[data-tab="${tabId}"]`);
  const panel = $(`#${tabId}`);
  if (!tab || !panel) {
    return false;
  }

  document.querySelectorAll(".nav-tab").forEach((node) => node.classList.remove("active"));
  document.querySelectorAll(".panel").forEach((node) => node.classList.remove("active"));
  tab.classList.add("active");
  panel.classList.add("active");
  setText("#screenTitle", tab.dataset.title);
  setText("#screenFooter", "");

  if (persist) {
    storeTab(tabId);
  }

  return true;
}

function setupTabs() {
  for (const tab of document.querySelectorAll(".nav-tab")) {
    tab.addEventListener("click", () => activateTab(tab.dataset.tab));
  }

  if (!activateTab(getStoredTab(), false)) {
    activateTab("home", false);
  }
}

function setUploadProgress(percent) {
  $("#otaProgressBar").style.width = `${Math.max(0, Math.min(100, percent))}%`;
}

function uploadFirmware(file) {
  return new Promise((resolve, reject) => {
    const request = new XMLHttpRequest();

    request.upload.addEventListener("progress", (event) => {
      if (event.lengthComputable) {
        const percent = Math.round((event.loaded / event.total) * 100);
        setUploadProgress(percent);
        setText("#otaMessage", `Uploading ${percent}%`);
      } else {
        setText("#otaMessage", "Uploading firmware...");
      }
    });

    request.addEventListener("load", () => {
      if (request.status >= 200 && request.status < 300) {
        setUploadProgress(100);
        resolve(request.responseText);
        return;
      }
      reject(new Error(request.responseText || `Upload failed with HTTP ${request.status}`));
    });

    request.addEventListener("error", () => reject(new Error("Firmware upload failed")));
    request.addEventListener("abort", () => reject(new Error("Firmware upload aborted")));
    request.open("POST", "/update");
    request.send(file);
  });
}

function setupFirmwareUpload() {
  const input = $("#firmwareFile");
  const message = $("#otaMessage");

  input.addEventListener("change", () => {
    $("#firmwareName").textContent = input.files[0]?.name || "Choose firmware .bin";
    setUploadProgress(0);
    message.textContent = "Idle";
  });

  $("#otaForm").addEventListener("submit", async (event) => {
    event.preventDefault();

    const file = input.files[0];
    if (!file) {
      message.textContent = "Select a firmware binary first.";
      showAlert("Firmware Update", "Select a firmware binary first.");
      return;
    }

    try {
      message.textContent = "Starting upload...";
      await uploadFirmware(file);
      message.textContent = "Update accepted. Device is restarting.";
      showAlert("Firmware Update", "Update accepted. The device is restarting.");
    } catch (error) {
      message.textContent = error.message;
      showAlert("Firmware Update Failed", error.message);
    }
  });
}

function setupWifiForm() {
  $("#togglePasswordButton").addEventListener("click", () => {
    const input = $("#password");
    const show = input.type === "password";
    input.type = show ? "text" : "password";
    $("#togglePasswordButton").setAttribute("aria-label", show ? "Hide password" : "Show password");
    $("#togglePasswordButton").setAttribute("title", show ? "Hide password" : "Show password");
  });

  $("#wifiForm").addEventListener("submit", async (event) => {
    event.preventDefault();

    const payload = {
      ssid: $("#ssid").value.trim(),
      password: $("#password").value,
    };

    setText("#wifiMessage", "Saving credentials...");
    const response = await fetch("/wifi", {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
      },
      body: JSON.stringify(payload),
    });

    if (!response.ok) {
      setText("#wifiMessage", await response.text());
      return;
    }

    setText("#wifiMessage", "Credentials saved. Device is restarting.");
    showAlert("WiFi", "Credentials saved. The device is restarting.");
  });

  $("#resetWifiButton").addEventListener("click", async () => {
    if (!confirm("Reset stored WiFi credentials and restart the device?")) {
      return;
    }

    setText("#wifiMessage", "Resetting WiFi credentials...");
    try {
      await postJson("/wifi/reset", "Failed to reset WiFi credentials");
      setText("#wifiMessage", "WiFi credentials reset. Device is restarting.");
      showAlert("WiFi Reset", "WiFi credentials reset. The device is restarting.");
    } catch (error) {
      setText("#wifiMessage", error.message);
      showAlert("WiFi Reset Failed", error.message);
    }
  });
}

function setupSampleConfigForm() {
  $("#sampleConfigForm").addEventListener("submit", async (event) => {
    event.preventDefault();

    const sampleIntervalMs = Number.parseInt($("#sampleIntervalMs").value, 10);
    if (!Number.isFinite(sampleIntervalMs) ||
        sampleIntervalMs < state.config.sampleIntervalMinMs ||
        sampleIntervalMs > state.config.sampleIntervalMaxMs) {
      setText("#sampleConfigMessage", `Enter a value from ${state.config.sampleIntervalMinMs} to ${state.config.sampleIntervalMaxMs} ms.`);
      return;
    }

    setText("#sampleConfigMessage", "Saving sample interval...");
    try {
      await postJson("/config/sample", "Failed to save sample interval", { sampleIntervalMs });
      setText("#sampleConfigMessage", "Sample interval saved. Device is restarting.");
      showAlert("Sampling", "Sample interval saved. The device is restarting.");
    } catch (error) {
      setText("#sampleConfigMessage", error.message);
      showAlert("Sampling Failed", error.message);
    }
  });
}

function setupDeviceActions() {
  $("#rebootButton").addEventListener("click", async () => {
    if (!confirm("Reboot the device now?")) {
      return;
    }

    setText("#deviceActionMessage", "Requesting reboot...");
    try {
      await postJson("/reboot", "Failed to reboot device");
      setText("#deviceActionMessage", "Device is restarting.");
      showAlert("Reboot", "The device is restarting.");
    } catch (error) {
      setText("#deviceActionMessage", error.message);
      showAlert("Reboot Failed", error.message);
    }
  });

  $("#resetConfigButton").addEventListener("click", async () => {
    if (!confirm("Reset all stored configuration and restart the device?")) {
      return;
    }

    setText("#deviceActionMessage", "Resetting configuration...");
    try {
      await postJson("/config/reset", "Failed to reset configuration");
      setText("#deviceActionMessage", "Configuration reset. Device is restarting.");
      showAlert("Configuration Reset", "All stored configuration was reset. The device is restarting.");
    } catch (error) {
      setText("#deviceActionMessage", error.message);
      showAlert("Configuration Reset Failed", error.message);
    }
  });
}

window.addEventListener("DOMContentLoaded", async () => {
  $("#firmwareLogo").innerHTML = firmwareLogo;

  setupTabs();
  setupSamplePlot();
  setupFirmwareUpload();
  setupWifiForm();
  setupSampleConfigForm();
  setupDeviceActions();
  renderConfig(state.config);

  try {
    await refreshConfig();
  } catch (error) {
    setText("#sampleConfigMessage", error.message);
  }

  setupSamplePolling();

  try {
    await refreshInfo();
  } catch (error) {
    showAlert("Information", error.message);
  }

  try {
    await refreshWifi();
  } catch (error) {
    setText("#wifiStatusText", error.message);
  }

  try {
    await refreshPartitions();
  } catch (error) {
    setText("#partitionMessage", error.message);
  }
});
