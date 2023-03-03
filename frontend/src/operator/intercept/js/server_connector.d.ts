import './server_connector.js'

declare function myTest(): any;

declare class DisplayInfo {
  display_id: string;
  width: number;
  height: number;
}

declare class DeviceDisplays {
  device_id: string;
  rotation: number;
  displays: DisplayInfo[];
}

declare class DeviceFrameMessage {
  static readonly TYPE_DISPLAYS_INFO: string;
  static [Symbol.hasInstance](instance: any): boolean;

  type: string;
  payload: any;
}
