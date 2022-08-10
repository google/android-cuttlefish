import { Injectable } from "@angular/core";
import { BehaviorSubject, Observable } from "rxjs";

@Injectable({
  providedIn: "root",
})
export class DisplaysService {
  private displays: string[] = [];
  private visibleDevicesChanged = new BehaviorSubject<string[]>([]);


  add(display: string) {
    this.displays.push(display);
    this.visibleDevicesChanged.next(this.displays);
  }

  remove(display: string) {
    this.displays = this.displays.filter((element) => element !== display);
    this.visibleDevicesChanged.next(this.displays);
  }

  getVisibleDevices() {
    return this.visibleDevicesChanged.asObservable();
  }
}
