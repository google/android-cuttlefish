import {Component} from '@angular/core';
import {DisplaysService} from '../displays.service';

@Component({
  selector: 'app-view-pane',
  templateUrl: './view-pane.component.html',
  styleUrls: ['./view-pane.component.sass'],
})
export class ViewPaneComponent {
  visibleDevices = this.displaysService.getVisibleDevices();

  constructor(public displaysService: DisplaysService) {}
}
