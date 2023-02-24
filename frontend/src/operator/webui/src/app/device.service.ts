import {Injectable} from '@angular/core';
import {map, mergeMap, shareReplay} from 'rxjs/operators';
import {Observable, Subject, merge} from 'rxjs';
import {HttpClient} from '@angular/common/http';
import {DeviceInfo} from './device-info-interface';
import {DomSanitizer} from '@angular/platform-browser';

@Injectable({
  providedIn: 'root',
})
export class DeviceService {
  private refreshSubject = new Subject<void>();
  private deviceFromServer = this.httpClient
    .get<string[]>('./devices')
    .pipe(
      map((deviceIds: string[]) =>
        deviceIds.sort((a, b) => a.localeCompare(b, undefined, {numeric: true}))
      )
    );

  private devices = merge(
    this.deviceFromServer,
    this.refreshSubject.pipe(mergeMap(() => this.deviceFromServer))
  ).pipe(shareReplay(1));

  constructor(
    private readonly httpClient: HttpClient,
    private sanitizer: DomSanitizer
  ) {}

  refresh(): void {
    this.refreshSubject.next();
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
