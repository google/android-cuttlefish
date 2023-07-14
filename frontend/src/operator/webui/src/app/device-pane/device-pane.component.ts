import {Component} from '@angular/core';
import {DeviceService} from '../device.service';
import {DisplaysService} from '../displays.service';
import {first, map, mergeMap} from 'rxjs/operators';
import {GroupService} from '../group.service';
import {ActivatedRoute} from '@angular/router';
import {DeviceInfo} from '../device-info-interface';

@Component({
  selector: 'app-device-pane',
  templateUrl: './device-pane.component.html',
  styleUrls: ['./device-pane.component.scss'],
})
export class DevicePaneComponent {
  groups = this.groupService.getGroups();
  devices = this.activatedRoute.queryParams.pipe(
    mergeMap(params => this.deviceService.getDevices(params['groupId'] ?? null))
  );

  constructor(
    private deviceService: DeviceService,
    public displaysService: DisplaysService,
    private groupService: GroupService,
    private activatedRoute: ActivatedRoute
  ) {}

  ngOnInit(): void {
    this.deviceService.refresh();
    this.groupService.refresh();
  }

  onRefresh(): void {
    this.deviceService.refresh();
    this.groupService.refresh();
  }

  showAll(): void {
    this.devices.pipe(first()).subscribe(devices => {
      devices.forEach(device => {
        if (!this.displaysService.isVisibleDevice(device.device_id)) {
          this.displaysService.toggleVisibility(device.device_id);
        }
      });
    });
  }
}
