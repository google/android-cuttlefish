import {Injectable} from '@angular/core';
import {map, Observable, of, Subject} from 'rxjs';
import {HttpClient} from '@angular/common/http';

@Injectable({
  providedIn: 'root',
})
export class DeviceService {

  private devicesObservable = new Observable<string[]>();

  constructor(private readonly httpClient: HttpClient) {}
  
  refresh(): Observable<string[]> {
    this.devicesObservable = this.httpClient.get<string[]>('/devices').pipe(map((devices) => devices.sort()));
    return this.devicesObservable;
  }
}
