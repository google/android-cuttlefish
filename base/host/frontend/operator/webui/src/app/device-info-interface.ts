export interface DisplayInfo {
  dpi: number;
  x_res: number;
  y_res: number;
}

export interface RegistrationInfo {
  displays: DisplayInfo[];
}

export interface DeviceInfo {
  device_id: string;
  registration_info: RegistrationInfo;
}
