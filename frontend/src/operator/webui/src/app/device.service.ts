import {Injectable, inject} from '@angular/core';
import {combineLatestWith, map, mergeMap, shareReplay} from 'rxjs/operators';
import {Observable, Subject, BehaviorSubject} from 'rxjs';
import {HttpClient} from '@angular/common/http';
import {DomSanitizer} from '@angular/platform-browser';
import {DeviceInfo} from './device-info-interface';
import {DeviceItem, DeviceGroup} from './device-item.interface';

function isLegacyDevice(device: DeviceItem) : boolean {
  return !device || !device.owner || !device.group_name || !device.name;
}

@Injectable({
  providedIn: 'root',
})
export class DeviceService {
  private readonly httpClient = inject(HttpClient);
  private sanitizer = inject(DomSanitizer);

  private refreshSubject = new BehaviorSubject<void>(undefined);

  private groupIdSubject = new Subject<string | null>();

  private devices = this.groupIdSubject.pipe(
    combineLatestWith(this.refreshSubject),
    mergeMap(([groupId, _]) => {
      if (groupId === null) {
        return this.allDeviceFromServer;
      }
      return this.getDevicesByGroupIdFromServer(groupId);
    }),
    shareReplay(1)
  );

  private deviceGroups = this.devices.pipe(map(this.groupsFromDeviceList))

  private legacyDevices = this.devices.pipe(map(this.legacyFromDeviceList));

  private allDeviceFromServer = this.httpClient
    .get<DeviceItem[]>('./devices')
    .pipe(map(this.sortDevices));

  private getDevicesByGroupIdFromServer(groupId: string) {
    return this.httpClient
      .get<DeviceItem[]>(`./devices?groupId=${groupId}`)
      .pipe(map(this.sortDevices));
  }

  private groupsFromDeviceList(devices: DeviceItem[]) {
    devices = devices.filter(d => !isLegacyDevice(d));
    const ownersCount = new Set(devices.map(d => d.owner)).size;
    let groupsByDisplayName = new Map<string, DeviceGroup>();
    for (const device of devices) {
      const groupDisplayName = ownersCount > 1 ?
          device.owner + ':' + device.group_name :
          device.group_name;
      let group = groupsByDisplayName.get(groupDisplayName);
      if (!group) {
        group = {
          owner: device.owner,
          name: device.group_name,
          displayName: groupDisplayName,
          devices: []
        };
        groupsByDisplayName.set(groupDisplayName, group);
      }
      group.devices.push(device);
    }
    return [...groupsByDisplayName.values()].sort(
        (g1, g2) => g1.displayName.localeCompare(
            g2.displayName, undefined, {numeric: true}));
  }

  private legacyFromDeviceList(devices: DeviceItem[]) {
    return devices.filter(isLegacyDevice)
  }

  private sortDevices(devices: DeviceItem[]) {
    return devices.sort((a: DeviceItem, b: DeviceItem) =>
      a['device_id'].localeCompare(b['device_id'], undefined, {numeric: true})
    );
  }

  refresh(): void {
    this.refreshSubject.next();
  }

  setGroupId(groupId: string | null) {
    this.groupIdSubject.next(groupId);
  }

  getLegacyDevices() {
    return this.legacyDevices;
  }

  getDeviceGroups() {
    return this.deviceGroups;
  }

  getAllDevices() {
    return this.devices;
  }

  getDeviceInfo(deviceId: string): Observable<DeviceInfo> {
    return this.httpClient
      .get('./devices/' + deviceId)
      .pipe(map(res => res as DeviceInfo));
  }
}
