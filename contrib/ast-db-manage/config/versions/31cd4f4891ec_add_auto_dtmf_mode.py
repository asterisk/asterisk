"""Add auto DTMF mode

Revision ID: 31cd4f4891ec
Revises: 23530d604b96
Create Date: 2015-04-10 12:36:51.619419

"""

# revision identifiers, used by Alembic.
revision = '31cd4f4891ec'
down_revision = '23530d604b96'

from alembic import op
from sqlalchemy.dialects.postgresql import ENUM
import sqlalchemy as sa

OLD_ENUM = ['rfc4733', 'inband', 'info']
NEW_ENUM = ['rfc4733', 'inband', 'info', 'auto']

old_type = sa.Enum(*OLD_ENUM, name='pjsip_dtmf_mode_values')
new_type = sa.Enum(*NEW_ENUM, name='pjsip_dtmf_mode_values_v2')

tcr = sa.sql.table('ps_endpoints', sa.Column('dtmf_mode', new_type,
                   nullable=True))

def upgrade():
    currentcontext = op.get_context()

    if currentcontext.bind.dialect.name == 'postgresql':
        # Upgrading to this revision WILL clear your directmedia values.
        enum = ENUM('rfc4733', 'inband', 'info', 'auto',
                    name='pjsip_dtmf_mode_values_v2')
        enum.create(op.get_bind(), checkfirst=False)

        op.execute('ALTER TABLE ps_endpoints ALTER COLUMN dtmf_mode TYPE'
                   ' pjsip_dtmf_mode_values_v2 USING'
                   ' dtmf_mode::text::pjsip_dtmf_mode_values_v2')

        ENUM(name="pjsip_dtmf_mode_values").drop(op.get_bind(), checkfirst=False)
        return
    if currentcontext.bind.dialect.name == 'oracle':
        # i not like else also in oracle not possible to alter constraint 
        # so we have to drop it and recreate . It is very important to use option novalidate which will force checks 
        # for all new updates and inserts but not do full table scan.  But as i see for our case  
        # we can safely validate it after creation , so lets do it
        op.execute('alter table ps_endpoints drop constraint PSEPDTMFMODE');
        op.execute('alter table ps_endpoints add constraint PSEPDTMFMODE  CHECK (dtmf_mode IN (\'rfc4733\', \'inband\', \'info\',\'auto\')) ENABLE VALIDATE');
        return
    op.alter_column('ps_endpoints', 'dtmf_mode',type_=new_type, existing_type=old_type)

def downgrade():
    currentcontext = op.get_context()
    
    #op.execute(tcr.update().where(tcr.c.directmedia==u'outgoing')
    #          .values(directmedia=None))

    if currentcontext.bind.dialect.name == 'postgresql':
        enum = ENUM('rfc4733', 'inband', 'info',
                    name='pjsip_dtmf_mode_values')
        enum.create(op.get_bind(), checkfirst=False)

        op.execute('ALTER TABLE ps_endpoints ALTER COLUMN dtmf_mode TYPE'
                   ' pjsip_dtmf_mode_values USING'
                   ' dtmf_mode::text::pjsip_dtmf_mode_values')

        ENUM(name="pjsip_dtmf_mode_values_v2").drop(op.get_bind(), checkfirst=False)
        return
    if currentcontext.bind.dialect.name == 'oracle':
        op.execute('alter table ps_endpoints drop constraint PSEPDTMFMODE');
        # it could be that 'auto' already used somewhere. So cannot validate
        op.execute('alter table ps_endpoints add constraint PSEPDTMFMODE  CHECK (dtmf_mode IN (\'rfc4733\', \'inband\', \'info\')) ENABLE NOVALIDATE');
        return
    op.alter_column('ps_endpoints', 'dtmf_mode',type_=old_type, existing_type=new_type)
