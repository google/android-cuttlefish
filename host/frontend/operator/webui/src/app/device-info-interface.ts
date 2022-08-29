export interface DisplayInfo {
  dpi: number;
  x_res: number;
  y_res: number;
}

export interface DeviceInfo {
  displays: DisplayInfo[];
}
