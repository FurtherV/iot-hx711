const state = {
  info: null,
  partitions: [],
  sample: null,
};

const $ = (selector) => document.querySelector(selector);

function setText(selector, value) {
  const node = $(selector);
  if (node) {
    node.textContent = value ?? "-";
  }
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

  setText("#connectionStatus", connectionLabel(wifi));
  setText("#networkMode", wifi.connected ? "Station" : wifi.softapActive ? "SoftAP" : "Offline");
  setText("#ipAddress", wifi.ip || "-");
  setText("#partition", info.partition || "-");

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
  ];

  for (const [label, value] of rows) {
    const term = document.createElement("dt");
    term.textContent = label;
    const description = document.createElement("dd");
    description.textContent = value ?? "-";
    details.append(term, description);
  }
}

function renderSample(sample) {
  state.sample = sample;
  const firstValue = sample.data?.[0];

  setText("#sampleValue", Number.isFinite(firstValue?.value) ? firstValue.value.toLocaleString(undefined, { maximumFractionDigits: 1 }) : "-");
  setText("#sampleUnit", firstValue?.unit || "g");
  setText("#sampleMeta", `Seq ${sample.sequence_number ?? "-"} / Inc ${sample.incarnation ?? "-"}`);
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

async function refreshInfo() {
  const response = await fetch("/api/info", { cache: "no-store" });
  if (!response.ok) {
    throw new Error("Unable to load device information");
  }
  renderInfo(await response.json());
}

async function refreshPartitions() {
  const response = await fetch("/api/partitions", { cache: "no-store" });
  if (!response.ok) {
    throw new Error("Unable to load partition table");
  }
  renderPartitions(await response.json());
}

async function refreshSample() {
  const response = await fetch("/sample", { cache: "no-store" });
  if (!response.ok) {
    throw new Error("Sample unavailable");
  }
  renderSample(await response.json());
}

function setupSamplePolling() {
  const poll = async () => {
    try {
      await refreshSample();
    } catch (error) {
      setText("#sampleMeta", state.sample ? "Sample unavailable" : error.message);
    }
  };

  poll();
  setInterval(poll, 1000);
}

function setupTabs() {
  for (const tab of document.querySelectorAll(".tab")) {
    tab.addEventListener("click", () => {
      document.querySelectorAll(".tab").forEach((node) => node.classList.remove("active"));
      document.querySelectorAll(".panel").forEach((node) => node.classList.remove("active"));
      tab.classList.add("active");
      $(`#${tab.dataset.tab}`).classList.add("active");
    });
  }
}

function setupFirmwareUpload() {
  const input = $("#firmwareFile");
  const message = $("#otaMessage");

  input.addEventListener("change", () => {
    $("#firmwareName").textContent = input.files[0]?.name || "Choose firmware .bin";
  });

  $("#otaForm").addEventListener("submit", async (event) => {
    event.preventDefault();

    const file = input.files[0];
    if (!file) {
      message.textContent = "Select a firmware binary first.";
      return;
    }

    message.textContent = "Uploading firmware...";
    const response = await fetch("/update", {
      method: "POST",
      body: file,
    });

    if (!response.ok) {
      message.textContent = await response.text();
      return;
    }

    message.textContent = "Update accepted. Device is restarting.";
  });
}

function setupWifiForm() {
  const message = $("#wifiMessage");

  $("#wifiForm").addEventListener("submit", async (event) => {
    event.preventDefault();

    const payload = {
      ssid: $("#ssid").value.trim(),
      password: $("#password").value,
    };

    message.textContent = "Saving credentials...";
    const response = await fetch("/api/wifi", {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
      },
      body: JSON.stringify(payload),
    });

    if (!response.ok) {
      message.textContent = await response.text();
      return;
    }

    message.textContent = "Credentials saved. Device is restarting.";
  });
}

window.addEventListener("DOMContentLoaded", async () => {
  setupTabs();
  setupFirmwareUpload();
  setupWifiForm();
  setupSamplePolling();

  try {
    await refreshInfo();
  } catch (error) {
    setText("#connectionStatus", "Unavailable");
    setText("#otaMessage", error.message);
  }

  try {
    await refreshPartitions();
  } catch (error) {
    setText("#partitionMessage", error.message);
  }
});
