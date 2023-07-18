import {Injectable} from '@angular/core';
import {combineLatestWith, map, mergeMap, shareReplay} from 'rxjs/operators';
import {Observable, Subject, BehaviorSubject} from 'rxjs';
import {HttpClient} from '@angular/common/http';
import {DomSanitizer} from '@angular/platform-browser';
import {DeviceInfo} from './device-info-interface';
import {DeviceItem} from './device-item.interface';

@Injectable({
  providedIn: 'root',
})
export class DeviceService {
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

  private allDeviceFromServer = this.httpClient
    .get<DeviceItem[]>('./devices')
    .pipe(map(this.sortDevices));

  private getDevicesByGroupIdFromServer(groupId: string) {
    return this.httpClient
      .get<DeviceItem[]>(`./devices?groupId=${groupId}`)
      .pipe(map(this.sortDevices));
  }

  constructor(
    private readonly httpClient: HttpClient,
    private sanitizer: DomSanitizer
  ) {}

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

  getDevices() {
    return this.devices;
  }

  getDeviceInfo(deviceId: string): Observable<DeviceInfo> {
    return this.httpClient
      .get('./devices/' + deviceId)
      .pipe(map(res => res as DeviceInfo));
  }
}
