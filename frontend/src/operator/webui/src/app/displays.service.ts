import {Injectable} from '@angular/core';
import {Subject, merge} from 'rxjs';
import {map, mergeMap} from 'rxjs/operators';
import {DeviceService} from './device.service';

@Injectable({
  providedIn: 'root',
})
export class DisplaysService {
  private devices;

  private visibleDeviceIds: string[] = [];
  private visibleDevicesChanged;

  private deviceVisibilityInfos;
  private displayInfoChanged;

  constructor(private deviceService: DeviceService) {
    this.devices = this.deviceService.getDevices();
    this.visibleDevicesChanged = new Subject<void>();
    this.deviceVisibilityInfos = merge(
      this.devices,
      this.visibleDevicesChanged.pipe(mergeMap(() => this.devices))
    ).pipe(
    map(devices =>
        devices.map(({device_id: deviceId}) => {
          return {id: deviceId, visible: this.isVisibleDevice(deviceId)};
        })
       )
    );
    this.displayInfoChanged = new Subject<any>();
  }

  toggleVisibility(deviceId: string): void {
    if (this.isVisibleDevice(deviceId)) {
      this.removeFromVisibleDevices(deviceId);
    } else {
      this.addToVisibleDevices(deviceId);
    }

    this.visibleDevicesChanged.next();
  }

  isVisibleDevice(display: string): boolean {
    return this.visibleDeviceIds.some(device => display === device);
  }

  getDeviceVisibilities() {
    return this.deviceVisibilityInfos;
  }

  getDisplayInfoChanged() {
    return this.displayInfoChanged.asObservable();
  }

  onDeviceDisplayInfo(deviceDisplays: any) {
    if (deviceDisplays.displays.length === 0) return;

    this.displayInfoChanged.next(deviceDisplays);
  }

  private addToVisibleDevices(addedDevice: string) {
    if (!this.isVisibleDevice(addedDevice)) {
      this.visibleDeviceIds.push(addedDevice);
    }
  }

  private removeFromVisibleDevices(removedDevice: string) {
    this.visibleDeviceIds = this.visibleDeviceIds.filter(
      visibleDevice => visibleDevice !== removedDevice
    );
  }
}
