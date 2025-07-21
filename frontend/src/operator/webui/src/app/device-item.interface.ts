export interface DeviceItem {
  device_id: string;
  group_name: string;
  owner: string;
  name: string;
  adb_port: number;
}

export interface DeviceGroup {
  owner: string;
  name: string;
  displayName: string;
  devices: DeviceItem[];
}

export interface DeviceFilter {
  owner: string | null;
  groupId: string | null;
}
